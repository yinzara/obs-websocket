/*
obs-websocket
Copyright (C) 2016-2017	Stéphane Lepin <stephane.lepin@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <QtWebSockets/QWebSocket>
#include <QtCore/QThread>
#include <QtCore/QByteArray>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QGuiApplication>
#include <QScreen>
#include <obs-frontend-api.h>
#include <QDesktopWidget>
#include <QApplication>
#include <QtMath>

#include "WSServer.h"
#include "obs-websocket.h"
#include "Config.h"
#include "Utils.h"
#include "QTimer.h"

#define STREAM_SERVICE_ID "websocket_custom_service"

#define WAMP_CONNECT_TIMEOUT 10000
#define WAMP_FUNCTION_PREFIX ".f."
#define WAMP_TOPIC_PREFIX ".t."

#include <wampcrauser.h>

QT_USE_NAMESPACE

WSServer* WSServer::Instance = nullptr;

WSServer::WSServer(QObject* parent)
    : QObject(parent),
      _wsServer(Q_NULLPTR),
      _clients(),
      _clMutex(QMutex::Recursive),
      _reconnectTimer(nullptr),
      _reconnectCount(1),
      _wampConnection(new WampConnection(this)),
      _wampStatus(WampConnectionStatus::Disconnected),
      _wampUrl(QUrl()),
      _wampId(QString()),
      _wampBaseUri(QString()),
      _wampRealm(QString()),
      _wampUser(QString()),
      _wampErrorUri(QString()),
      _wampErrorMessage(QString()),
      _canPublish(true)
{
    _wsServer = new QWebSocketServer(
        QStringLiteral("obs-websocket"),
        QWebSocketServer::NonSecureMode, this);
    
    _wampConnection->subscribeMeta = false;
    
    connect(_wampConnection, &WampConnection::connected,
            this, &WSServer::onWampConnected);
    connect(_wampConnection, &WampConnection::disconnected,
            this, &WSServer::onWampDisconnected);
    connect(_wampConnection, &WampConnection::error,
            this, &WSServer::onWampError);
}

WSServer::~WSServer() {
    Stop();
    StopWamp();
    delete _wampConnection;
}

void WSServer::Start(quint16 port) {
    if (port == _wsServer->serverPort())
        return;

    if(_wsServer->isListening())
        Stop();

    bool serverStarted = _wsServer->listen(QHostAddress::Any, port);
    if (serverStarted) {
        blog(LOG_INFO, "server started successfully on TCP port %d", port);

        connect(_wsServer, SIGNAL(newConnection()),
            this, SLOT(onNewConnection()));
    }
    else {
        QString errorString = _wsServer->errorString();
        blog(LOG_ERROR,
            "error: failed to start server on TCP port %d: %s",
            port, errorString.toUtf8().constData());

        QMainWindow* mainWindow = (QMainWindow*)obs_frontend_get_main_window();

        obs_frontend_push_ui_translation(obs_module_get_string);
        QString title = tr("OBSWebsocket.Server.StartFailed.Title");
        QString msg = tr("OBSWebsocket.Server.StartFailed.Message").arg(port);
        obs_frontend_pop_ui_translation();

        QMessageBox::warning(mainWindow, title, msg);
    }
}

void WSServer::Stop() {
    _clMutex.lock();
    for(QWebSocket* pClient : _clients) {
        pClient->close();
    }
    _clMutex.unlock();

    _wsServer->close();

    blog(LOG_INFO, "server stopped successfully");
}

void WSServer::StartWamp(QUrl url, QString realm, QString baseUri, QString id, QString user, QString password)
{
    if (url == _wampUrl && realm == _wampRealm) {
        _wampId = id;
        _wampBaseUri = baseUri;
        if (!user.isEmpty()) {
            _wampConnection->setUser(new WampCraUser(user, password));
        }
        return;
    }
    
    if (_wampStatus != WampConnectionStatus::Disconnecting && _wampStatus != WampConnectionStatus::Disconnected)
        _wampConnection->disconnect();
    
    _wampStatus = WampConnectionStatus::Connecting;
    _wampConnection->setUrl(url);
    _wampConnection->setRealm(realm);

    if (!user.isEmpty())
    {
        _wampConnection->setUser(new WampCraUser(user, password));
    }
    else
    {
        _wampConnection->setUser(nullptr);
    }
    
    _reconnectCount = 1;

    _wampId = id;
    _wampBaseUri = baseUri;
    _wampRealm = realm;
    _wampUrl = url;
    _wampUser = user;
    

    qInfo() << "WAMP Connecting:"
        << "\nurl: " << url
        << "\nrealm: " << realm
        << "\nid: " << id
        << "\nuser: " << user
        << "\npassword: " << password
        << "\nbaseuri: " << baseUri;

    //set a reconnect timer for 10 seconds to start the reconnection process should the initial connect fail
    if (_reconnectTimer)
    {
        _reconnectTimer->stop();
        _reconnectTimer->deleteLater();
    }
    _reconnectTimer = new QTimer(this);
    _reconnectTimer->setSingleShot(true);
    _reconnectTimer->setInterval(WAMP_CONNECT_TIMEOUT); // 10 second connect timeout
    _reconnectTimer->setTimerType(Qt::TimerType::CoarseTimer);
    connect(_reconnectTimer, &QTimer::timeout,
            this, &WSServer::onWampConnectTimeout);
    _reconnectTimer->start();
    
    _wampConnection->connect();
    
}

void WSServer::StopWamp()
{
    _wampStatus = WampConnectionStatus::Disconnecting;
    _wampConnection->disconnect();
    _wampConnection->setUser(nullptr);
    
    cancelWampReconnect();
    
    _wampId = QString();
    _wampRealm = QString();
    _wampUrl = QUrl();
    _wampUser = QString();
}


void WSServer::onWampConnected()
{
    _reconnectCount = 1;
    cancelWampReconnect();
    _wampStatus = WampConnectionStatus::Connected;
    
    qInfo() << "wamp " << _wampUrl << " connected";
    
    obs_frontend_push_ui_translation(obs_module_get_string);
    QString title = tr("OBSWebsocket.WampNotifyConnected.Title");
    QString msg = tr("OBSWebsocket.WampNotifyConnected.Message")
    .arg(_wampUrl.toString());
    
    obs_frontend_pop_ui_translation();
    
    Utils::WampSysTrayNotify(msg, QSystemTrayIcon::Information, title);

    QMetaObject::invokeMethod(this, "RegisterWamp");
}

void WSServer::onWampDisconnected()
{
    _wampStatus = WampConnectionStatus::Disconnected;
    
    disconnect(_wampConnection, &WampConnection::disconnected,
               this, &WSServer::onWampDisconnected);
    
    qInfo() << "wamp " << _wampUrl << " disconnected";
    
    obs_frontend_push_ui_translation(obs_module_get_string);
    QString title = tr("OBSWebsocket.WampNotifyDisconnected.Title");
    QString msg = tr("OBSWebsocket.WampNotifyConnected.Message")
        .arg(_wampUrl.toString().toUtf8().constData());
    
    obs_frontend_pop_ui_translation();
    
    Utils::WampSysTrayNotify(msg, QSystemTrayIcon::Information, title);
    
    scheduleWampReconnect();
}

void WSServer::onWampError(const WampError& error)
{
    if (_wampStatus == WampConnectionStatus::Disconnecting || _wampStatus == WampConnectionStatus::Error)
        return; //ignore errors while disconnecting or if already in an error state
    
    _wampErrorUri = error.uri().toString();
        
    QVariantList args = error.args();
    if (!args.isEmpty())
        _wampErrorMessage = args.first().toString();
    else
        _wampErrorMessage = _wampErrorUri;
    
    QVariantMap details = error.details();
    if (!details.isEmpty())
    {
        _wampErrorData = Utils::DataFromMap(details);
    }
    else
    {
        obs_data_clear(_wampErrorData);
    }
    
    //fall back to anonymous if the connection attempt fails and a user was specified and we are configured to fallback
    if ((_wampStatus == WampConnectionStatus::Connecting || _wampStatus == WampConnectionStatus::Disconnected) && !_wampUrl.isEmpty() && Config::Current()->WampAnonymousFallback && !_wampConnection->user()->name().isEmpty()) {
        qInfo() << "Connection error while connecting. Attempting anonymous fallback";
        
        QUrl url = _wampUrl;
        QString realm = _wampRealm;
        QString baseuri = _wampBaseUri;
        QString wampid = _wampId;
        
        StopWamp();
        QMetaObject::invokeMethod(this, "StartWamp", Qt::QueuedConnection,
                                  Q_ARG(QUrl, url),
                                  Q_ARG(QString, realm),
                                  Q_ARG(QString, baseuri),
                                  Q_ARG(QString, wampid));
        
        return;
    }
    if (_wampStatus != WampConnectionStatus::Connected)
        _wampStatus = WampConnectionStatus::Error;
    
    blog(LOG_INFO, "wamp %s error %s (%s)",
         _wampUrl.toString().toUtf8().constData(),
         _wampErrorMessage.toUtf8().constData(),
         _wampErrorUri.toUtf8().constData());
    
    cancelWampReconnect();
    scheduleWampReconnect();
    
    obs_frontend_push_ui_translation(obs_module_get_string);
    QString title = tr("OBSWebsocket.WampNotifyError.Title");
    QString msg = tr("OBSWebsocket.WampNotifyError.Message")
         .arg(_wampUrl.toString())
         .arg(_wampErrorMessage)
         .arg(_wampErrorUri);
    
    obs_frontend_pop_ui_translation();
    
    Utils::WampSysTrayNotify(msg, QSystemTrayIcon::Information, title);
}

void WSServer::onWampConnectTimeout()
{
    cancelWampReconnect();
    qDebug() << "wamp connection timeout. try again";
    
    scheduleWampReconnect();
}

void WSServer::onWampReconnectTimeout()
{
    StopWamp();
    qDebug() << "wamp reconnect timeout occurred, reconnnecting...";
    
    Config* config = Config::Current();
    if (config->WampEnabled)
        QMetaObject::invokeMethod(this, "StartWamp",
                              Q_ARG(QUrl, config->WampUrl),
                              Q_ARG(QString, config->WampRealm),
                              Q_ARG(QString, config->WampBaseUri),
                              Q_ARG(QString, config->WampIdEnabled?config->WampId:""),
                              Q_ARG(QString, config->WampAuthEnabled?config->WampUser:""),
                              Q_ARG(QString, config->WampAuthEnabled?config->WampPassword:""));
}

void WSServer::cancelWampReconnect()
{
    if (_reconnectTimer != Q_NULLPTR)
    {
        qDebug() << "cancelling wamp reconnect";
        disconnect(_reconnectTimer, &QTimer::timeout,
                   this, &WSServer::onWampConnectTimeout);
        
        disconnect(_reconnectTimer, &QTimer::timeout,
                   this, &WSServer::onWampReconnectTimeout);
        
        _reconnectTimer->stop();
        
        _reconnectTimer->deleteLater();
        _reconnectTimer = Q_NULLPTR;
    }
}

void WSServer::scheduleWampReconnect()
{
    if (_reconnectTimer == Q_NULLPTR)
    {
        int interval = qrand() % ((int)(qPow(2.0, _reconnectCount) - 1.0) * 1000);
        qDebug() << "scheduling wamp reconnect with _reconnectCount : " << _reconnectCount << " and interval : " << interval;
        _reconnectTimer = new QTimer(this);
        _reconnectTimer->setSingleShot(true);
        _reconnectTimer->setInterval(interval); // exponential backoff [generate pseudorandom number between 0 and (2^k-1)*1000 milliseconds] where k is the number of reconnection attepts
        _reconnectTimer->setTimerType(Qt::TimerType::CoarseTimer);
        connect(_reconnectTimer, &QTimer::timeout,
                this, &WSServer::onWampReconnectTimeout);
        _reconnectTimer->start();
        
        if (_reconnectCount < 5) //only go to a max of 5 so that the reconnect interval never goes above 30 seconds (i.e. 2^5-1=31)
            _reconnectCount++;
    }
    else
        qDebug() << "not scheduling reconnect as timer is already in place";
}

QString WSServer::WampTopic(QString value)
{
    QString topic = Utils::WampUrlFix(value).prepend(WAMP_TOPIC_PREFIX).prepend(_wampBaseUri);
    if (!_wampId.isEmpty())
        topic = topic.append('.').append(Utils::WampUrlFix(_wampId));
    return topic;
}

QString WSServer::WampProcedure(QString value)
{
    QString topic = Utils::WampUrlFix(value)
        .prepend(WAMP_FUNCTION_PREFIX)
        .prepend(_wampBaseUri);
    
    if (!_wampId.isEmpty())
        topic = topic.append('.').append(Utils::WampUrlFix(_wampId));
    
    return topic;
}

void WSServer::Broadcast(const char* type, obs_data_t* message) {
    
    if (_wampStatus == WampConnectionStatus::Connected && _canPublish)
    {
        QString topic = WampTopic(QString(type));
        QVariantList list = Utils::ListFromData(message);
        _wampConnection->publish(topic, list);
    }
    
    obs_data_set_string(message, "update-type", type);
    QString json = obs_data_get_json(message);
    
    _clMutex.lock();
    for(QWebSocket* pClient : _clients) {
        if (Config::Current()->AuthRequired
            && (pClient->property(PROP_AUTHENTICATED).toBool() == false)) {
            // Skip this client if unauthenticated
            continue;
        }
        
        pClient->sendTextMessage(json);
    }
    _clMutex.unlock();
    
    if (Config::Current()->DebugEnabled)
        blog(LOG_DEBUG, "Update << '%s'", json.toUtf8().constData());
    
}

void WSServer::onNewConnection() {
    QWebSocket* pSocket = _wsServer->nextPendingConnection();
    if (pSocket) {
        connect(pSocket, SIGNAL(textMessageReceived(const QString&)),
            this, SLOT(onTextMessageReceived(QString)));
        connect(pSocket, SIGNAL(disconnected()),
            this, SLOT(onSocketDisconnected()));

        pSocket->setProperty(PROP_AUTHENTICATED, false);

        _clMutex.lock();
        _clients << pSocket;
        _clMutex.unlock();

        QHostAddress clientAddr = pSocket->peerAddress();
        QString clientIp = Utils::FormatIPAddress(clientAddr);

        blog(LOG_INFO, "new client connection from %s:%d",
            clientIp.toUtf8().constData(), pSocket->peerPort());

        obs_frontend_push_ui_translation(obs_module_get_string);
        QString title = tr("OBSWebsocket.NotifyConnect.Title");
        QString msg = tr("OBSWebsocket.NotifyConnect.Message")
            .arg(Utils::FormatIPAddress(clientAddr));
        obs_frontend_pop_ui_translation();

        Utils::SysTrayNotify(msg, QSystemTrayIcon::Information, title);
    }
}

void WSServer::onTextMessageReceived(QString message) {
    QWebSocket* pSocket = qobject_cast<QWebSocket*>(sender());
    if (pSocket) {
        WSWebSocketRequestHandler handler(pSocket);
        handler.processIncomingMessage(message);
    }
}

void WSServer::onSocketDisconnected() {
    QWebSocket* pSocket = qobject_cast<QWebSocket*>(sender());
    if (pSocket) {
        pSocket->setProperty(PROP_AUTHENTICATED, false);

        _clMutex.lock();
        _clients.removeAll(pSocket);
        _clMutex.unlock();;

        pSocket->deleteLater();

        QHostAddress clientAddr = pSocket->peerAddress();
        QString clientIp = Utils::FormatIPAddress(clientAddr);

        blog(LOG_INFO, "client %s:%d disconnected",
            clientIp.toUtf8().constData(), pSocket->peerPort());

        obs_frontend_push_ui_translation(obs_module_get_string);
        QString title = tr("OBSWebsocket.NotifyDisconnect.Title");
        QString msg = tr("OBSWebsocket.NotifyDisconnect.Message")
            .arg(Utils::FormatIPAddress(clientAddr));
        obs_frontend_pop_ui_translation();

        Utils::SysTrayNotify(msg, QSystemTrayIcon::Information, title);
    }
}

void WSServer::RegisterWamp()
{
    QString regProc = Config::Current()->WampRegisterProcedure;
    if (!regProc.isEmpty())
    {
        qInfo() << "Calling WAMP registration procedure: " << regProc;
        OBSSourceAutoRelease scene = obs_frontend_get_current_scene();
        QString sceneCollection = obs_frontend_get_current_scene_collection();
        QString sceneName = obs_source_get_name(scene) ;
        QString profile = obs_frontend_get_current_profile();
        bool recording = obs_frontend_recording_active();
        bool streaming = obs_frontend_streaming_active();
        
        OBSService service = obs_frontend_get_streaming_service();
        QString serviceType = obs_service_get_type(service);
        OBSDataAutoRelease settings = obs_service_get_settings(service);
        QVariantMap settingsMap = Utils::MapFromData(settings);
        
        QVariantList nics;
        for (QNetworkInterface interf : QNetworkInterface::allInterfaces())
        {
            QVariantMap ifData;
            ifData["name"] = interf.name();
            if (interf.name() != interf.humanReadableName())
                ifData["t"] = interf.humanReadableName();
            ifData["index"] = interf.index();
            
            if (! interf.hardwareAddress().isEmpty())
                ifData["mac"] = interf.hardwareAddress();
            
            for (QNetworkAddressEntry addr : interf.addressEntries())
            {
                if (!addr.ip().isLoopback()) // loopbacks
                {
                    QString ip = addr.ip().toString();
                    if (ip.contains(':'))
                        ifData["ip6"] = ip;
                    else
                    {
                        ifData["ip"] = ip;
                        if (!addr.netmask().toString().isEmpty())
                            ifData["netmask"] = addr.netmask().toString();
                        if (!addr.broadcast().toString().isEmpty())
                            ifData["broadcast"] = addr.broadcast().toString();
                    }
                }
            }
            nics.append(ifData);
        }
        
        QVariantList screens;
        int i = 0;
        QDesktopWidget* desktop = QApplication::desktop();
        desktop->screen();
        for (QScreen* screen : QGuiApplication::screens())
        {
            QVariantMap screenData;
            screenData["index"] = i++;
            if (screen->isPortrait(screen->orientation()))
                screenData["orientation"] = "P";
            else
                screenData["orientation"] = "L";
            if (screen->refreshRate() > 0)
                screenData["refresh"] = screen->refreshRate();
            if (!screen->serialNumber().isEmpty())
                screenData["serial"] = screen->serialNumber();
            if (!screen->name().isEmpty())
                screenData["name"] = screen->name();
            if (!screen->model().isEmpty())
                screenData["model"] = screen->model();
            if (!screen->manufacturer().isEmpty())
                screenData["manufacturer"] = screen->manufacturer();
            
            QRect geom = screen->geometry();
            if (geom.x() > 0)
                screenData["x"] = geom.x();
            if (geom.y() > 0)
                screenData["y"] = geom.y();
            screenData["width"] = geom.width();
            screenData["height"] = geom.height();
            
            screens.append(screenData);
        }
            
        
        QVariantMap stream {
            { "type" , serviceType },
            { "settings", settingsMap }
        };
        QVariantMap r {
            { "id" , _wampId },
            { "user", _wampUser },
            { "url", _wampUrl },
            { "realm", _wampRealm },
            { "baseuri", _wampBaseUri },
            { "nics", nics },
            { "screens", screens },
            { "scene-collection", sceneCollection },
            { "scene-name", sceneName },
            { "profile", profile },
            { "recording", recording },
            { "streaming", streaming },
            { "stream", stream }
        };
        QVariantList args { r };
        QVariantMap options {
            {"disclose_me", true }
        };
        
        _wampConnection->call2(regProc, args, [this](QVariant v) {
            this->CompleteWampRegistration(v);
        }, options);
    }
    else
    {
        QMetaObject::invokeMethod(this, "RegisterWampProcedures", Qt::QueuedConnection);
    }
}

void WSServer::CompleteWampRegistration(QVariant v)
{
    Config* config = Config::Current();
    char* sc = obs_frontend_get_current_scene_collection();
    QString currentSceneCollection = sc;
    bfree(sc);
    OBSSourceAutoRelease scene = obs_frontend_get_current_scene();
    QString currentSceneName = obs_source_get_name(scene);
    char* prof = obs_frontend_get_current_profile();
    QString currentProfile = prof;
    bfree(prof);
    bool currentlyStreaming = obs_frontend_streaming_active();
    bool currentlyRecording = obs_frontend_recording_active();
    
    QVariantMap r;
    if (v.type() == QVariant::Map) {
        r = v.toMap();
    } else if (v.type() == QVariant::List) {
        r = v.toList()[0].toMap();
    }
    QString id = r["id"].toString();
    QString user = r["user"].toString();
    QString secret = r["secret"].toString();
    if (secret.isEmpty())
        secret = r["password"].toString();
    QUrl url = r["url"].toUrl();
    QString realm = r["realm"].toString();
    QString baseuri = r["baseuri"].toString();
    QString sceneCollection = r["scene-collection"].toString();
    QString sceneName = r["scene-name"].toString();
    QString profile = r["profile"].toString();
    QVariantMap stream = r["stream"].toMap();
    
    
    bool save = r["save"].toBool();
    bool reconnect = r["reconnect"].toBool();
    bool canRegister = r.contains("register") ? r["register"].toBool() : true;
    _canPublish = r.contains("publish") ? r["publish"].toBool() : true;
    bool recording = r["recording"].toBool();
    bool streaming = r["streaming"].toBool();
    
    
    bool projectChanged  = false;
    
    qInfo() << "Received registration response from WAMP server:"
    << "\nid: " << id
    << "\nsecret: " << secret
    << "\nuser: " << user
    << "\nurl: " << url
    << "\nrealm: " << realm
    << "\nbaseurl: " << baseuri
    << "\nscene-collection: " << sceneCollection
    << "\nscene-name: " << sceneName
    << "\nprofile: " << profile
    << "\nsave: " << save
    << "\nreconnect: " << reconnect
    << "\nregister: " << canRegister
    << "\npublish: " << _canPublish;
    
    
    if (!id.isEmpty() && id != config->WampId)
    {
        config->WampId = id;
        _wampId = id;
    }
    if (!baseuri.isEmpty() && baseuri != config->WampBaseUri)
    {
        config->WampBaseUri = baseuri;
        _wampBaseUri = baseuri;
    }
    if (!user.isEmpty() && user != config->WampUser)
    {
        config->WampUser = user;
        _wampUser = user;
    }
    if (!secret.isEmpty() && secret != config->WampPassword)
    {
        config->WampPassword = secret;
        _wampConnection->setUser(new WampCraUser(config->WampUser, config->WampPassword));
    }
    if (!url.isEmpty() && url != config->WampUrl)
    {
        config->WampUrl = url;
        _wampUrl = url;
        _wampConnection->setUrl(url);
    }
    if (!realm.isEmpty() && realm != config->WampRealm)
    {
        config->WampRealm = realm;
        _wampRealm = realm;
        _wampConnection->setRealm(realm);
    }
    
    if (!profile.isEmpty() && profile != currentProfile)
    {
        obs_frontend_set_current_profile(profile.toUtf8());
    }
    if (!sceneCollection.isEmpty() && sceneCollection != currentSceneCollection)
    {
        projectChanged = true;
        obs_frontend_set_current_scene_collection(sceneCollection.toUtf8());
    }
    if (!sceneName.isEmpty() && sceneName != currentSceneName)
    {
        projectChanged = true;
        OBSSourceAutoRelease scene = obs_get_source_by_name(sceneName.toUtf8());
        if (scene)
            obs_frontend_set_current_scene(scene);
    }
    if (streaming != currentlyStreaming && r.contains("streaming"))
    {
        if (currentlyStreaming)
            obs_frontend_streaming_stop();
        else
            obs_frontend_streaming_start();
    }
    
    if (recording != currentlyRecording && r.contains("recording"))
    {
        if (currentlyRecording)
            obs_frontend_recording_stop();
        else
            obs_frontend_recording_start();
    }
        
    if (!stream.isEmpty())
    {
        OBSService service = obs_frontend_get_streaming_service();
        QString currentServiceType = obs_service_get_type(service);
        OBSDataAutoRelease currentSettings = obs_service_get_settings(service);
        
        QString requestedType = stream["type"].toString();
        QVariantMap settingsMap = stream["settings"].toMap();
        if (!settingsMap.isEmpty())
        {
            OBSDataAutoRelease settings = Utils::DataFromMap(settingsMap);
            if (!requestedType.isEmpty() && requestedType != currentServiceType) {
                OBSDataAutoRelease hotkeys = obs_hotkeys_save_service(service);
                service = obs_service_create(requestedType.toUtf8(), STREAM_SERVICE_ID, settings, hotkeys);
                obs_service_release(service);  // obs_service_create adds a ref which means you need
                // remove here
            } else {
                // If type isn't changing, we should overlay the settings we got
                // to the existing settings. The default obs_service_update does this
                obs_service_update(service, settings);
            }
            obs_frontend_set_streaming_service(service);
            
            if (save)
                obs_frontend_save_streaming_service();
        }
    }
    
    if (save)
    {
        config->Save();
        if (projectChanged)
            obs_frontend_save();
    }
    
    if (reconnect)
    {
        qInfo() << "WAMP reg response indicated need to reconnect with alternative information. Reconnecting...";

        StopWamp();
        QMetaObject::invokeMethod(this, "StartWamp",
          Q_ARG(QUrl, config->WampUrl),
          Q_ARG(QString, config->WampRealm),
          Q_ARG(QString, config->WampBaseUri),
          Q_ARG(QString, config->WampIdEnabled?config->WampId:""),
          Q_ARG(QString, config->WampAuthEnabled?config->WampUser:""),
          Q_ARG(QString, config->WampAuthEnabled?config->WampPassword:""));
    }
    else if (canRegister)
    {
        QMetaObject::invokeMethod(this, "RegisterWampProcedures");
    }
    else if (_canPublish)
    {
        QMetaObject::invokeMethod(this, "BroadcastWampConnected");
    }
}

void WSServer::BroadcastWampConnected()
{
    OBSDataAutoRelease data = obs_data_create();
    obs_data_set_string(data, "url", _wampUrl.toString().toUtf8());
    obs_data_set_string(data, "realm", _wampRealm.toUtf8());
    obs_data_set_string(data, "id", Utils::WampUrlFix(_wampId).toUtf8());
    Broadcast("WampConnected", data);
}

void WSServer::RegisterWampProcedures()
{
    qDebug() << "Registering wamp procedures";
    
    for (QHash<QString,void(*)(WSRequestHandler*)>::iterator i = WSRequestHandler::messageMap.begin();i != WSRequestHandler::messageMap.end();i++) {
        QString procedure = WampProcedure(i.key());
        void(*handlerMethod)(WSRequestHandler*) = i.value();
        _wampConnection->registerProcedure(procedure, [procedure, handlerMethod] (QVariantList args) {
            if (Config::Current()->DebugEnabled)
                qDebug() << "Call procedure " << procedure << " with args: " << args;
            WSWampRequestHandler handler(handlerMethod);
            return handler.processIncomingMessage(args);
        });
    }
    
    if (_canPublish)
        QMetaObject::invokeMethod(this, "BroadcastWampConnected");
}

WSServer::WampConnectionStatus WSServer::GetWampStatus()
{
    return _wampStatus;
}

QString WSServer::GetWampErrorMessage()
{
    return _wampErrorMessage;
}

QString WSServer::GetWampErrorUri()
{
    return _wampErrorUri;
}

OBSDataAutoRelease WSServer::GetWampErrorData()
{
    return _wampErrorData;
}
