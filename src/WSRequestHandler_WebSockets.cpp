#include <QString>
#include "Utils.h"
#include "Config.h"
#include "WSRequestHandler.h"
#include "WSServer.h"

/**
 * Gets the current WebSocket settings. Requires authentication.
 *
 * @return {bool} `server-enabled` Enable the local websocket server
 * @return {int} `server-port` The port the web socket server is bound to
 * @return {string} `auth` The password used for authentication of the local websocket
 * @return {bool} `auth-required` Is authentication required for the local websocket
 * @return {bool} `alerts-enabled` Does the application show desktop alerts?
 * @return {bool} `debug-enabled` Is debug logging of websocket messages enabled?
 *
 * @api requests
 * @name GetWampSettings
 * @category websockets
 * @since 4.3
 */
void WSRequestHandler::HandleGetWebSocketSettings(WSRequestHandler* req)
{
    Config* config = Config::Current();
    obs_data_t* response = obs_data_create();
    obs_data_set_bool(response, "server-enabled", config->ServerEnabled);
    obs_data_set_int(response, "server-port", config->ServerPort);
    obs_data_set_bool(response, "auth-required", config->AuthRequired);
    obs_data_set_bool(response, "alerts-enabled", config->AlertsEnabled);
    obs_data_set_bool(response, "debug-enabled", config->DebugEnabled);

    req->SendOKResponse(response);
    obs_data_release(response);
}

/**
* Sets the current WebSocket settings. Requires authentication.
*
* @param {bool} `server-enabled` Enable the local websocket server
* @param {int} `server-port` The port the web socket server is bound to
* @param {string} `auth` The password used for authentication of the local websocket
* @param {bool} `auth-required` Is authentication required for the local websocket? Ignored and assumed true if 'auth' is specified as a non empty string.
* @param {bool} `alerts-enabled` Does the application show desktop alerts?
* @param {bool} `debug-enabled` Is debug logging of websocket messages enabled?
*
* @api requests
* @name SetWampSettings
* @category websockets
* @since 4.3
*/
void WSRequestHandler::HandleSetWebSocketSettings(WSRequestHandler* req)
{
    if (!req->hasField("server-enabled") && !req->hasField("server-port") && !req->hasField("auth-required") && !req->hasField("auth") && !req->hasField("alerts-enabled") && !req->hasField("debug-enabled"))
    {
        req->SendErrorResponse("missing request parameters (one or more): server-enabled, auth-required, auth, server-port, debug-enabled or alerts-enabled");
        return;
    }

    Config* config = Config::Current();

    QString auth = obs_data_get_string(req->data, "auth");
    
    if (!auth.isEmpty())
    {
        config->AuthRequired = true;
        config->SetPassword(auth);
    }
    else if (req->hasField("auth-required"))
    {
        config->AuthRequired = obs_data_get_bool(req->data, "auth-required");
    }

    bool restartLocalServer = false;
    if (req->hasField("server-enabled"))
    {
        bool enabled = obs_data_get_bool(req->data, "server-enabled");
        if (enabled != config->ServerEnabled)
        {
            config->ServerEnabled = enabled;
            restartLocalServer = true;
        }
    }
    if (req->hasField("server-port"))
    {
        int port = obs_data_get_int(req->data, "server-port");
        if (port > 0 && (uint64_t) port != config->ServerPort)
        {
            config->ServerPort = port;
            restartLocalServer = true;
        }
    }
    
    if (req->hasField("alerts-enabled"))
    {
        config->AlertsEnabled = obs_data_get_bool(req->data, "alerts-enabled");
    }
    
    if (req->hasField("debug-enabled"))
    {
        config->DebugEnabled = obs_data_get_bool(req->data, "debug-enabled");
    }

    if (restartLocalServer)
    {
        WSServer::Instance->Stop();
        if (config->ServerEnabled)
        {
            WSServer::Instance->Start(config->ServerPort);
        }
    }

    if (obs_data_get_bool(req->data, "save"))
    {
        config->Save();
    }

    req->SendOKResponse();
}

