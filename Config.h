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

#ifndef CONFIG_H
#define CONFIG_H

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include <QUrl>

class Config
{
	public:
		Config();
		~Config();
		void Load();
		void Save();

		void SetPassword(const char *password);
		bool CheckAuth(const char *userChallenge);
		const char* GenerateSalt();
		static const char* GenerateSecret(
			const char *password, const char *salt);

		bool ServerEnabled;
		uint64_t ServerPort;
		
		bool DebugEnabled;

		bool AuthRequired;
		const char *Secret;
		const char *Salt;
		const char *SessionChallenge;
		QUrl WSServerUrl;
		bool SettingsLoaded;
		
		static Config* Current();

	private:
		static Config *_instance;
		mbedtls_entropy_context entropy;
		mbedtls_ctr_drbg_context rng;
};

#endif // CONFIG_H
