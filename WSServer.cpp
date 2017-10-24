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
#include <QHostInfo>
#include <QAbstractSocket>
#include <QNetworkInterface>
#include <QProcessEnvironment>
#include <obs-frontend-api.h>

#include "WSServer.h"
#include "WSEvents.h"
#include "obs-websocket.h"
#include "Config.h"
#include "Utils.h"

#define DEFAULT_WS_ORIGIN QStringLiteral("https://github.com/Palakis/obs-websocket")
#define WS_HOSTNAME_ENV_VARIABLE QStringLiteral("WS_HOSTNAME")
#define WS_ORIGIN_ENV_VARIABLE QStringLiteral("WS_ORIGIN")
#define CONNECT_TIMEOUT 10000

QT_USE_NAMESPACE

WSServer* WSServer::Instance = nullptr;

static const char* GetServerStatus(WSServer::WSRemoteControlServerStatus status)
{
	switch (status)
	{
		case WSServer::ConnectingState:
			return "CONNECTING";
		case WSServer::ConnectedState:
			return "CONNECTED";
		case WSServer::ErrorState:
			return "ERROR";
		case WSServer::DisconnectedState:
			return "DISCONNECTED";
	}
}

WSServer::WSServer(QObject* parent) :
	QObject(parent),
	_wsServer(Q_NULLPTR),
	_clients(),
	_clMutex(QMutex::Recursive),
	_serverConnection(Q_NULLPTR),
	_serverUrl(QUrl()),
	_reconnectTimer(Q_NULLPTR),
	_currentStatus(WSRemoteControlServerStatus::DisconnectedState),
	_reconnectCount(1),
	_lastRemoteError(QString())
{
	_serverThread = new QThread();

	_wsServer = new QWebSocketServer(
		QStringLiteral("obs-websocket"),
		QWebSocketServer::NonSecureMode,
		_serverThread);

	_serverThread->start();
}

obs_data_t* WSServer::GetRemoteControlServerData()
{
	obs_data_t* response = obs_data_create();
	WSServer::WSRemoteControlServerStatus status = WSServer::Instance->GetRemoteControlServerStatus();
	obs_data_set_string(response, "rc-status", GetServerStatus(status));
	
	QUrl remoteUrl = WSServer::Instance->GetRemoteControlServerUrl();
	if (!remoteUrl.isEmpty())
		obs_data_set_string(response, "url", remoteUrl.toString().toUtf8().constData());
	
	
	QString remoteError = WSServer::Instance->GetRemoteControlServerError();
	if (!remoteError.isEmpty())
		obs_data_set_string(response, "error", remoteError.toUtf8().constData());

	return response;
}

WSServer::~WSServer()
{
	Stop();
	delete _serverThread;
}

void WSServer::Start(quint16 port)
{
	if (port == _wsServer->serverPort())
		return;

	if(_wsServer->isListening())
		Stop();

	bool serverStarted = _wsServer->listen(QHostAddress::Any, port);
	if (serverStarted)
	{
		connect(_wsServer, &QWebSocketServer::newConnection,
			this, &WSServer::onNewConnection);
	}
}

void WSServer::Stop()
{
	_clMutex.lock();
	for(QWebSocket* pClient : _clients) {
		pClient->close();
	}
	_clMutex.unlock();

	_wsServer->close();
}

void WSServer::broadcast(obs_data_t* message)
{
	_clMutex.lock();
	
	const char* json = obs_data_get_json(message);
	const char* updateType = obs_data_get_string(message, "update-type");

	for(QWebSocket* pClient : _clients) {
		if (Config::Current()->AuthRequired
			&& (pClient->property(PROP_AUTHENTICATED).toBool() == false)
			&& (updateType == nullptr || !WSEvents::authNotRequired.contains(updateType)))
		{
			// Skip this client if unauthenticated
			continue;
		}

		pClient->sendTextMessage(json);
	}

	if (_serverConnection) {
		_serverConnection->sendTextMessage(json);
	}
	
	
	if (Config::Current()->DebugEnabled)
		blog(LOG_DEBUG, "Update << '%s'", json);

	_clMutex.unlock();
}

QString WSServer::GetLocalHostname()
{
	QString localHostname = QProcessEnvironment::systemEnvironment().value(WS_HOSTNAME_ENV_VARIABLE, QHostInfo::localHostName());
	if (localHostname.indexOf('.') > 0)
		localHostname = localHostname.left(localHostname.indexOf('.'));
	return localHostname;
}

void WSServer::ConnectToServer(QUrl url)
{
	if (_serverUrl == url && _serverConnection != Q_NULLPTR && _serverConnection->state() == QAbstractSocket::SocketState::ConnectedState)
	{
		return; // do nothing if the server is connected and the url isn't changing
	}
	
	if (_serverUrl != url)
	{
		_reconnectCount = 1; //reset the counter if it's a new URL
	}
	
	DisconnectFromServer();
	
	_currentStatus = WSRemoteControlServerStatus::ConnectingState;
	
	_serverUrl = url;
	
	_serverConnection = new QWebSocket(QProcessEnvironment::systemEnvironment().value(WS_ORIGIN_ENV_VARIABLE, DEFAULT_WS_ORIGIN), QWebSocketProtocol::VersionLatest, this);
	
	connect(_serverConnection, &QWebSocket::connected,
			this, &WSServer::onServerConnection);
	
	connect(_serverConnection, &QWebSocket::disconnected,
			this, &WSServer::onServerDisconnect);
	
	connect(_serverConnection, static_cast<void(QWebSocket::*)(QAbstractSocket::SocketError)>(&QWebSocket::error), this, &WSServer::onServerError);
	
	_lastRemoteError = QString();
	
	_serverConnection->open(url);
	
	//set a reconnect timer for 10 seconds to start the reconnection process should the initial connect fail
	_reconnectTimer = new QTimer(this);
	_reconnectTimer->setSingleShot(true);
	_reconnectTimer->setInterval(CONNECT_TIMEOUT); // 10 second connect timeout
	_reconnectTimer->setTimerType(Qt::TimerType::CoarseTimer);
	connect(_reconnectTimer, &QTimer::timeout,
			this, &WSServer::onServerConnectTimeout);
	_reconnectTimer->start();
	
	WSEvents::Instance->OnRemoteControlServerStateChange();
	
	blog(LOG_INFO, "opening server connection to %s",
			url.toString().toUtf8().constData());
}

void WSServer::DisconnectFromServer()
{
	if (_serverConnection != Q_NULLPTR)
	{
		_serverUrl = QUrl();
		QWebSocket* server = _serverConnection;
		onServerDisconnect();
		server->close();
		
		cancelReconnect();
	}
}

WSServer::WSRemoteControlServerStatus WSServer::GetRemoteControlServerStatus()
{
	return _currentStatus;
}

QUrl WSServer::GetRemoteControlServerUrl()
{
	return _serverUrl;
}

void WSServer::onServerConnection()
{
	if (_serverConnection != Q_NULLPTR)
	{
		cancelReconnect();
		
		if (!_lastRemoteError.isEmpty()) {
			_lastRemoteError = QString();
		}
		
		_reconnectCount = 1;
		_currentStatus = WSRemoteControlServerStatus::ConnectedState;
		
		connect(_serverConnection, &QWebSocket::textMessageReceived,
			this, &WSServer::textMessageReceived);
		_serverConnection->setProperty(PROP_AUTHENTICATED, true); // server connections are automatically authenticated since they were outbound (i.e. we should trust it)
		
		obs_data_t* connectMsg = obs_data_create();
		obs_data_set_string(connectMsg, "update-type", "ClientConnected");
		obs_data_set_string(connectMsg, "hostname", GetLocalHostname().toUtf8().constData());
		
		obs_service_t* service = obs_frontend_get_streaming_service();
		obs_data_t* settings = obs_service_get_settings(service);
		if (obs_data_has_user_value(settings, "password"))
		{
			obs_data_set_string(connectMsg, "access-key", obs_data_get_string(settings, "password"));
		}
		obs_data_release(settings);
		
		_serverConnection->sendTextMessage(QString(obs_data_get_json(connectMsg)));

		obs_data_release(connectMsg);
		
		WSEvents::Instance->OnRemoteControlServerStateChange();
	}
}

QString WSServer::GetRemoteControlServerError()
{
	return _lastRemoteError;
}

void WSServer::onServerError(QAbstractSocket::SocketError error)
{
	_lastRemoteError = _serverConnection->errorString();
	
	blog(LOG_INFO, "server connection error %s",
			_lastRemoteError .toUtf8().constData());
	if (_currentStatus != WSRemoteControlServerStatus::DisconnectedState && _currentStatus != WSRemoteControlServerStatus::ErrorState)
	{
		_currentStatus = WSRemoteControlServerStatus::ErrorState;
		WSEvents::Instance->OnRemoteControlServerStateChange();
	}
	scheduleServerReconnect();
}

void WSServer::onServerConnectTimeout()
{
	cancelReconnect();
	
	_currentStatus = WSRemoteControlServerStatus::ErrorState;
	_lastRemoteError = QStringLiteral("Connection Timeout");
	
	scheduleServerReconnect();
}

void WSServer::scheduleServerReconnect()
{
	if (!_serverUrl.isEmpty() && _reconnectTimer == Q_NULLPTR && (_serverConnection == Q_NULLPTR || _serverConnection->state() != QAbstractSocket::ConnectedState))
	{
		_reconnectTimer = new QTimer(this);
		_reconnectTimer->setSingleShot(true);
		_reconnectTimer->setInterval(qrand() % ((int)(qPow(2.0, _reconnectCount) - 1.0) * 1000)); // exponential backoff [generate pseudorandom number between 0 and (2^k-1)*1000 milliseconds] where k is the number of reconnection attepts
		_reconnectTimer->setTimerType(Qt::TimerType::CoarseTimer);
		connect(_reconnectTimer, &QTimer::timeout,
				this, &WSServer::onReconnect);
		_reconnectTimer->start();
		
		if (_reconnectCount < 5) //only go to a max of 5 so that the reconnect interval never goes above 30 seconds (i.e. 2^5-1=31)
			_reconnectCount++;
	}
}

void WSServer::onReconnect()
{
	cancelReconnect();
	
	ConnectToServer(_serverUrl);
}

void WSServer::cancelReconnect()
{
	if (_reconnectTimer != Q_NULLPTR)
	{
		_reconnectTimer->stop();
		
		disconnect(_reconnectTimer, &QTimer::timeout,
				   this, &WSServer::onReconnect);
		
		disconnect(_reconnectTimer, &QTimer::timeout,
				this, &WSServer::onServerConnectTimeout);
		
		_reconnectTimer = Q_NULLPTR;
	}

}

void WSServer::onServerDisconnect()
{
	if (_serverConnection != Q_NULLPTR)
	{
		disconnect(_serverConnection, &QWebSocket::connected,
				   this, &WSServer::onServerConnection);
		
		disconnect(_serverConnection, &QWebSocket::disconnected,
				   this, &WSServer::onServerDisconnect);
		
		disconnect(_serverConnection, static_cast<void(QWebSocket::*)(QAbstractSocket::SocketError)>(&QWebSocket::error),
				   this, &WSServer::onServerError);
		
		disconnect(_serverConnection, &QWebSocket::textMessageReceived,
				   this, &WSServer::textMessageReceived);
		
		if (_serverConnection->closeCode() > QWebSocketProtocol::CloseCodeNormal) {
			if (_lastRemoteError.isEmpty())
				_lastRemoteError = _serverConnection->closeReason();
			//abnormal close, increment connection attempts to 5 so that retries are attempted at the max inteval
			_reconnectCount = 5;
		}
		
		_serverConnection = Q_NULLPTR;
		
		if (_currentStatus != WSRemoteControlServerStatus::ErrorState)
			_currentStatus = WSRemoteControlServerStatus::DisconnectedState;
		
		WSEvents::Instance->OnRemoteControlServerStateChange();
		
		if (!_serverUrl.isEmpty())
			scheduleServerReconnect();
	}
}

void WSServer::onNewConnection()
{
	QWebSocket* pSocket = _wsServer->nextPendingConnection();

	if (pSocket)
	{
		connect(pSocket, &QWebSocket::textMessageReceived,
			this, &WSServer::textMessageReceived);
		connect(pSocket, &QWebSocket::disconnected,
			this, &WSServer::socketDisconnected);
		pSocket->setProperty(PROP_AUTHENTICATED, false);

		_clMutex.lock();
		_clients << pSocket;
		_clMutex.unlock();

		QHostAddress clientAddr = pSocket->peerAddress();
		QString clientIp = Utils::FormatIPAddress(clientAddr);

		blog(LOG_INFO, "new client connection from %s:%d",
			clientIp.toUtf8().constData(), pSocket->peerPort());

		QString msg = QString(obs_module_text("OBSWebsocket.ConnectNotify.ClientIP"))
			+ QString(" ")
			+ clientAddr.toString();

		Utils::SysTrayNotify(msg,
			QSystemTrayIcon::Information,
			QString(obs_module_text("OBSWebsocket.ConnectNotify.Connected")));
	}
}

void WSServer::textMessageReceived(QString message)
{
	QWebSocket* pSocket = qobject_cast<QWebSocket*>(sender());

	if (pSocket)
	{
		WSRequestHandler handler(pSocket);
		handler.processIncomingMessage(message);
	}
}


void WSServer::socketDisconnected()
{
	QWebSocket* pSocket = qobject_cast<QWebSocket*>(sender());

	if (pSocket)
	{
		pSocket->setProperty(PROP_AUTHENTICATED, false);

		_clMutex.lock();
		_clients.removeAll(pSocket);
		_clMutex.unlock();

		pSocket->deleteLater();

		QHostAddress clientAddr = pSocket->peerAddress();
		QString clientIp = Utils::FormatIPAddress(clientAddr);

		blog(LOG_INFO, "client %s:%d disconnected",
			clientIp.toUtf8().constData(), pSocket->peerPort());

		QString msg = QString(obs_module_text("OBSWebsocket.ConnectNotify.ClientIP"))
			+ QString(" ")
			+ clientAddr.toString();

		Utils::SysTrayNotify(msg,
			QSystemTrayIcon::Information,
			QString(obs_module_text("OBSWebsocket.ConnectNotify.Disconnected")));
	}
}
