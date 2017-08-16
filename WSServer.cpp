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

WSServer::WSServer(QObject* parent) :
	QObject(parent),
	_wsServer(Q_NULLPTR),
	_clients(),
	_clMutex(QMutex::Recursive),
	_serverConnection(Q_NULLPTR),
	_serverUrl(QUrl()),
	_reconnectTimer(Q_NULLPTR),
	_currentStatus(WSRemoteControlServerStatus::DisconnectedState),
	_reconnectCount(1)
{
	_serverThread = new QThread();

	_wsServer = new QWebSocketServer(
		QStringLiteral("obs-websocket"),
		QWebSocketServer::NonSecureMode,
		_serverThread);

	_serverThread->start();
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

void WSServer::broadcast(QString message)
{
	_clMutex.lock();

	for(QWebSocket* pClient : _clients) {
		if (Config::Current()->AuthRequired
			&& (pClient->property(PROP_AUTHENTICATED).toBool() == false))
		{
			// Skip this client if unauthenticated
			continue;
		}

		pClient->sendTextMessage(message);
	}

	if (_serverConnection) {
		_serverConnection->sendTextMessage(message);
	}

	_clMutex.unlock();
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
	
	_serverConnection->open(url);
	
	//set a reconnect timer for 10 seconds to start the reconnection process should the initial connect fail
	_reconnectTimer = new QTimer(this);
	_reconnectTimer->setSingleShot(true);
	_reconnectTimer->setInterval(CONNECT_TIMEOUT); // 10 second connect timeout
	_reconnectTimer->setTimerType(Qt::TimerType::CoarseTimer);
	connect(_reconnectTimer, &QTimer::timeout,
			this, &WSServer::onServerConnectTimeout);
	_reconnectTimer->start();
	
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
		_currentStatus = WSRemoteControlServerStatus::DisconnectedState;
		
		if (_reconnectTimer != Q_NULLPTR)
		{
			_reconnectTimer->stop();
			
			disconnect(_reconnectTimer, &QTimer::timeout,
					   this, &WSServer::onReconnect);
			_reconnectTimer = Q_NULLPTR;
		}
	}
}

WSServer::WSRemoteControlServerStatus WSServer::GetRemoteControlServerStatus()
{
	return _currentStatus;
}

void WSServer::onServerConnection()
{
	if (_serverConnection != Q_NULLPTR)
	{
		cancelReconnect();
		
		_reconnectCount = 1;
		_currentStatus = WSRemoteControlServerStatus::ConnectedState;
		
		_clMutex.lock();
		
		connect(_serverConnection, &QWebSocket::textMessageReceived,
			this, &WSServer::textMessageReceived);
		_serverConnection->setProperty(PROP_AUTHENTICATED, true); // server connections are automatically authenticated since they were outbound (i.e. we should trust it)
		
		obs_data_t* connectMsg = obs_data_create();
		obs_data_set_string(connectMsg, "update-type", "ClientConnected");
		obs_data_set_string(connectMsg, "hostname", QProcessEnvironment::systemEnvironment().value(WS_HOSTNAME_ENV_VARIABLE, QHostInfo::localHostName()).toUtf8().constData());
		
		
		_serverConnection->sendTextMessage(QString(obs_data_get_json(connectMsg)));

		_clMutex.unlock();

		obs_data_release(connectMsg);
		
		WSEvents::Instance->OnRemoteControlServerConnected();
	}
}

void WSServer::onServerError(QAbstractSocket::SocketError error)
{
	WSEvents::Instance->OnRemoteControlServerError();
	blog(LOG_INFO, "server connection error %s",
			_serverConnection->errorString().toUtf8().constData());
	if (_currentStatus != WSRemoteControlServerStatus::DisconnectedState)
	{
		_currentStatus = WSRemoteControlServerStatus::ErrorState;
	}
	scheduleServerReconnect();
}

void WSServer::onServerConnectTimeout()
{
	cancelReconnect();
	
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

QWebSocket* WSServer::GetRemoteControlWebSocket()
{
	return _serverConnection;
}

void WSServer::onServerDisconnect()
{
	if (_serverConnection != Q_NULLPTR)
	{
		WSEvents::Instance->OnRemoteControlServerDisconnected();
		
		disconnect(_serverConnection, &QWebSocket::connected,
				   this, &WSServer::onServerConnection);
		
		disconnect(_serverConnection, &QWebSocket::disconnected,
				   this, &WSServer::onServerDisconnect);
		
		disconnect(_serverConnection, static_cast<void(QWebSocket::*)(QAbstractSocket::SocketError)>(&QWebSocket::error),
				   this, &WSServer::onServerError);
		
		disconnect(_serverConnection, &QWebSocket::textMessageReceived,
				   this, &WSServer::textMessageReceived);
		
		_serverConnection = Q_NULLPTR;
		
		if (_currentStatus != WSRemoteControlServerStatus::ErrorState)
			_currentStatus = WSRemoteControlServerStatus::DisconnectedState;
		
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
