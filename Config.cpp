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

#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pkcs5.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <string>

#include "Config.h"

#define SECTION_NAME "WebsocketAPI"
#define PARAM_ENABLE "ServerEnabled"
#define PARAM_PORT "ServerPort"
#define PARAM_DEBUG "DebugEnabled"
#define PARAM_AUTHREQUIRED "AuthRequired"
#define PARAM_SECRET "AuthSecret"
#define PARAM_SALT "AuthSalt"
#define PARAM_WS_SERVER_ENABLE "WSSeverEnable"
#define PARAM_WS_SERVER_URL "WSServerUrl"
#define PARAM_STATUS_INTERVAL "StatusInterval"

Config* Config::Instance = nullptr;

Config::Config(QObject *parent) :
	QObject(parent)
{
	// Default settings
	ServerEnabled = true;
	ServerPort = 4444;
	
	DebugEnabled = false;

	AuthRequired = false;
	Secret = "";
	Salt = "";
	SettingsLoaded = false;
	WSServerUrl = QUrl(QString(Salt));
	WSServerEnabled = false;
	
	StatusUpdateIntervalSec = 2;

	// OBS Config defaults
	config_t* obs_config = obs_frontend_get_global_config();
	if (obs_config)
	{
		config_set_default_bool(obs_config,
			SECTION_NAME, PARAM_ENABLE, ServerEnabled);
		config_set_default_uint(obs_config,
			SECTION_NAME, PARAM_PORT, ServerPort);

		config_set_default_bool(obs_config,
			SECTION_NAME, PARAM_DEBUG, DebugEnabled);

		config_set_default_bool(obs_config,
			SECTION_NAME, PARAM_AUTHREQUIRED, AuthRequired);
		config_set_default_string(obs_config,
			SECTION_NAME, PARAM_SECRET, Secret);
		config_set_default_string(obs_config,
			SECTION_NAME, PARAM_SALT, Salt);

		config_set_default_string(obs_config,
			SECTION_NAME, PARAM_WS_SERVER_URL, "");
		
		config_set_default_bool(obs_config,
			SECTION_NAME, PARAM_WS_SERVER_ENABLE, WSServerEnabled);

		config_set_default_int(obs_config,
			SECTION_NAME, PARAM_STATUS_INTERVAL, StatusUpdateIntervalSec);
	}

	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init(&rng);
	mbedtls_ctr_drbg_seed(&rng, mbedtls_entropy_func, &entropy, nullptr, 0);
	//mbedtls_ctr_drbg_set_prediction_resistance(&rng, MBEDTLS_CTR_DRBG_PR_ON);

	SessionChallenge = GenerateSalt();
}

Config::~Config()
{
	mbedtls_ctr_drbg_free(&rng);
	mbedtls_entropy_free(&entropy);
	bfree((void*) SessionChallenge);
	bfree((void*) Secret);
	bfree((void*) Salt);
}

void Config::Load()
{
	config_t* obs_config = obs_frontend_get_global_config();

	ServerEnabled = config_get_bool(obs_config, SECTION_NAME, PARAM_ENABLE);
	ServerPort = config_get_uint(obs_config, SECTION_NAME, PARAM_PORT);
	
	DebugEnabled = config_get_bool(obs_config, SECTION_NAME, PARAM_DEBUG);

	AuthRequired = config_get_bool(obs_config, SECTION_NAME, PARAM_AUTHREQUIRED);
	Secret = bstrdup(config_get_string(obs_config, SECTION_NAME, PARAM_SECRET));
	Salt = bstrdup(config_get_string(obs_config, SECTION_NAME, PARAM_SALT));

	WSServerUrl = QUrl(QString(config_get_string(obs_config, SECTION_NAME, PARAM_WS_SERVER_URL)));
	WSServerEnabled = config_get_bool(obs_config, SECTION_NAME, PARAM_WS_SERVER_ENABLE);
	
	StatusUpdateIntervalSec = config_get_int(obs_config, SECTION_NAME, PARAM_STATUS_INTERVAL);
}

void Config::Save()
{
	config_t* obs_config = obs_frontend_get_global_config();

	config_set_bool(obs_config, SECTION_NAME, PARAM_ENABLE, ServerEnabled);
	config_set_uint(obs_config, SECTION_NAME, PARAM_PORT, ServerPort);
	
	config_set_bool(obs_config, SECTION_NAME, PARAM_DEBUG, DebugEnabled);

	config_set_bool(obs_config, SECTION_NAME, PARAM_AUTHREQUIRED, AuthRequired);
	config_set_string(obs_config, SECTION_NAME, PARAM_SECRET, Secret);
	config_set_string(obs_config, SECTION_NAME, PARAM_SALT, Salt);
	config_set_string(obs_config, SECTION_NAME, PARAM_WS_SERVER_URL, WSServerUrl.toString().toUtf8().constData());
	config_set_bool(obs_config, SECTION_NAME, PARAM_WS_SERVER_ENABLE, WSServerEnabled);
	config_set_int(obs_config, SECTION_NAME, PARAM_STATUS_INTERVAL, StatusUpdateIntervalSec);
	
	config_save(obs_config);
}

const char* Config::GenerateSalt()
{
	// Generate 32 random chars
	unsigned char* random_chars = (unsigned char*)bzalloc(32);
	mbedtls_ctr_drbg_random(&rng, random_chars, 32);

	// Convert the 32 random chars to a base64 string
	char* salt = (char*)bzalloc(64);
	size_t salt_bytes;
	mbedtls_base64_encode(
		(unsigned char*)salt, 64, &salt_bytes,
		random_chars, 32);

	bfree(random_chars);
	return salt;
}

const char* Config::GenerateSecret(const char *password, const char *salt)
{
	// Concatenate the password and the salt
	std::string passAndSalt = "";
	passAndSalt += password;
	passAndSalt += salt;

	// Generate a SHA256 hash of the password
	unsigned char* challengeHash = (unsigned char*)bzalloc(32);
	mbedtls_sha256(
		(unsigned char*)passAndSalt.c_str(), passAndSalt.length(),
		challengeHash, 0);
	
	// Encode SHA256 hash to Base64
	char* challenge = (char*)bzalloc(64);
	size_t challenge_bytes = 0;
	mbedtls_base64_encode(
		(unsigned char*)challenge, 64, &challenge_bytes,
		challengeHash, 32);
	bfree(challengeHash);
	return challenge;
}

void Config::SetPassword(const char *password)
{
	const char *new_salt = GenerateSalt();
	const char *new_challenge = GenerateSecret(password, new_salt);
	
	bfree((void*) Secret);
	bfree((void*) Salt);
	this->Salt = new_salt;
	this->Secret = new_challenge;
}

bool Config::CheckAuth(const char *response)
{
	// Concatenate auth secret with the challenge sent to the user
	std::string challengeAndResponse = "";
	challengeAndResponse += this->Secret;
	challengeAndResponse += this->SessionChallenge;

	// Generate a SHA256 hash of challengeAndResponse
	unsigned char* hash = (unsigned char*)bzalloc(32);
	mbedtls_sha256(
		(unsigned char*)challengeAndResponse.c_str(),
		challengeAndResponse.length(),
		hash, 0);

	// Encode the SHA256 hash to Base64
	char* expected_response = (char*)bzalloc(64);
	size_t base64_size = 0;
	mbedtls_base64_encode(
		(unsigned char*)expected_response, 64, &base64_size,
		hash, 32);

	bool authSuccess = false;
	if (strcmp(expected_response, response) == 0) {
		bfree((void*) SessionChallenge);
		SessionChallenge = GenerateSalt();
		authSuccess = true;
	}

	bfree(hash);
	bfree(expected_response);
	return authSuccess;
}

Config* Config::Current()
{
	return Config::Instance;
}
