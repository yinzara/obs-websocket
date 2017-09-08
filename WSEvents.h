/*
obs-websocket
Copyright (C) 2016-2017	Stéphane Lepin <stephane.lepin@gmail.com>
Copyright (C) 2017	Brendan Hagan <https://github.com/haganbmj>

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

#ifndef WSEVENTS_H
#define WSEVENTS_H

#include <obs-frontend-api.h>
#include <QListWidgetItem>
#include "WSServer.h"

class WSEvents : public QObject
{
	Q_OBJECT

	public:
		explicit WSEvents(WSServer* srv);
		~WSEvents();
		static void FrontendEventHandler(
			enum obs_frontend_event event, void* private_data);
		void connectTransitionSignals(obs_source_t* transition);
		void connectSceneSignals(obs_source_t* scene);
		static WSEvents* Instance;

		uint64_t GetStreamingTime();
		const char* GetStreamingTimecode();
		uint64_t GetRecordingTime();
		const char* GetRecordingTimecode();
	
		void OnRemoteControlServerStateChange();
		void SetStatusInterval(int secs);

	private Q_SLOTS:
		void deferredInitOperations();
		void StreamStatus();
		void TransitionDurationChanged(int ms);
		void SelectedSceneChanged(
			QListWidgetItem* current, QListWidgetItem* prev);
		void ModeSwitchClicked(bool checked);
	
	private:
		WSServer* _srv;
		signal_handler_t* transition_handler;
		signal_handler_t* scene_handler;

		bool _streaming_active;
		bool _recording_active;

		uint64_t _stream_starttime;
		uint64_t _rec_starttime;

		uint64_t _lastBytesSent;
		uint64_t _lastBytesSentTime;
	
		QTimer* _statusTimer;

		void broadcastUpdate(const char* updateType,
			obs_data_t* additionalFields);

		void OnSceneChange();
		void OnSceneListChange();
		void OnSceneCollectionChange();
		void OnSceneCollectionListChange();

		void OnTransitionChange();
		void OnTransitionListChange();

		void OnProfileChange();
		void OnProfileListChange();

		void OnStreamStarting();
		void OnStreamStarted();
		void OnStreamStopping();
		void OnStreamStopped();

		void OnRecordingStarting();
		void OnRecordingStarted();
		void OnRecordingStopping();
		void OnRecordingStopped();

		void OnExit();

		static void OnTransitionBegin(void* param, calldata_t* data);

		static void OnSceneReordered(void* param, calldata_t* data);
		static void OnSceneItemAdd(void* param, calldata_t* data);
		static void OnSceneItemDelete(void* param, calldata_t* data);
		static void OnSceneItemVisibilityChanged(void* param, calldata_t* data);
};

#endif // WSEVENTS_H
