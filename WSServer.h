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

#include <QtCore/QObject>
#include <QtCore/QList>
#include <QtCore/QMutex>

#include <QAbstractSocket>

QT_FORWARD_DECLARE_CLASS(QWebSocketServer)
QT_FORWARD_DECLARE_CLASS(QWebSocket)

#include "WSRequestHandler.h"

class WSServer : public QObject
{
	Q_OBJECT

	public:
		explicit WSServer(QObject* parent = Q_NULLPTR);
		virtual ~WSServer();
		void Start(quint16 port);
		void Stop();
		void broadcast(QString message);
		void ConnectToServer(QUrl url);
		void DisconnectFromServer();
		QWebSocket* GetRemoteControlWebSocket();
		static WSServer* Instance;
	

	private Q_SLOTS:
		void onNewConnection();
		void onServerConnection();
		void onServerError(QAbstractSocket::SocketError error);
		void textMessageReceived(QString message);
		void socketDisconnected();
		void onServerDisconnect();

	private:
		QWebSocketServer* _wsServer;
		QList<QWebSocket*> _clients;
		QMutex _clMutex;
		QThread* _serverThread;
		QWebSocket* _serverConnection;
};

#endif // WSSERVER_H
