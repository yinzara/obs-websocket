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

#ifndef WSSERVER_H
#define WSSERVER_H

#include <QtMath>

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QMutex>

#include <QTimer>
#include <QAbstractSocket>

#include <obs-data.h>

QT_FORWARD_DECLARE_CLASS(QWebSocketServer)
QT_FORWARD_DECLARE_CLASS(QWebSocket)

#include "WSRequestHandler.h"

class WSServer : public QObject
{
	
	
	Q_OBJECT

	public:
	
		enum WSRemoteControlServerStatus {
			ConnectedState,
			ConnectingState,
			DisconnectedState,
			ErrorState
		};
		Q_ENUM(WSRemoteControlServerStatus)
	
		explicit WSServer(QObject* parent = Q_NULLPTR);
		virtual ~WSServer();
		void Start(quint16 port);
		void Stop();
		void broadcast(obs_data_t* message);
		void ConnectToServer(QUrl url);
		void DisconnectFromServer();
		QString GetRemoteControlServerError();
		WSRemoteControlServerStatus GetRemoteControlServerStatus();
		QUrl GetRemoteControlServerUrl();
		QString GetLocalHostname();
		obs_data_t* GetRemoteControlServerData();
		static WSServer* Instance;

	private Q_SLOTS:
		void onNewConnection();
		void onServerConnection();
		void onServerError(QAbstractSocket::SocketError error);
		void textMessageReceived(QString message);
		void socketDisconnected();
		void onServerDisconnect();
		void scheduleServerReconnect();
		void onReconnect();
		void onServerConnectTimeout();
		void cancelReconnect();
		void onPingTimeout();
		void onPongTimeout();
		void onPong(quint64 elapsedTime, const QByteArray &payload);

	private:
		QWebSocketServer* _wsServer;
		QList<QWebSocket*> _clients;
		QMutex _clMutex;
		QThread* _serverThread;
		QWebSocket* _serverConnection;
		QUrl _serverUrl;
		QTimer* _reconnectTimer;
		WSRemoteControlServerStatus _currentStatus;
		qreal _reconnectCount;
		QString _lastRemoteError;
};

#endif // WSSERVER_H
