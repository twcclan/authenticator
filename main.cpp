#include "version.h"
#include <q_shared.h>
#include <g_local.h>
#include <qmmapi.h>
#include "main.h"
#include <stdio.h>
#include <time.h>
#include <vector>

#define TIMER_START clock_t timer_start = clock() / (CLOCKS_PER_SEC / 1000);
#define TIMER_END clock_t time_elapsed = (clock() / (CLOCKS_PER_SEC / 1000))  - timer_start;
#define TIMER_PRINT(x) if(time_elapsed > 1000) g_syscall(G_PRINT, QMM_VARARGS("WARNING: Function %s took %i ms\n", #x, time_elapsed));

pluginres_t* g_result = NULL;
plugininfo_t g_plugininfo = {
	"TWC Authenticator",	//name of plugin
	AUTH_QMM_VERSION,			//version of plugin
	AUTH_QMM_DESC,			//description of plugin
	AUTH_QMM_BUILDER,			//author of plugin
	AUTH_QMM_WEBSITE,		//website of plugin
	0,					//can this plugin be paused?
	1,					//can this plugin be loaded via cmd
	1,					//can this plugin be unloaded via cmd
	QMM_PIFV_MAJOR,				//plugin interface version major
	QMM_PIFV_MINOR				//plugin interface version minor
};

#define AUTH_URL "http://board.twcclan.org/misc.php?twc=auth"

eng_syscall_t g_syscall = NULL;
mod_vmMain_t g_vmMain = NULL;
pluginfuncs_t* g_pluginfuncs = NULL;
int g_vmbase = 0;
int active_transfers = 0;
int auth_user = -1;
const char hashChars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
CURLM *curl_handle = NULL;
http_t *game_http = NULL;
buffer_t *console_buffer = NULL;
bool authenticator_need_preload = false;

playerinfo_t g_playerinfo[MAX_CLIENTS];

/**
	Parse the userinfo string and add all key value pairs to the post data
	
	Returns:
		the lastptr returned by curl_formadd (or NULL if no data was added)
*/
void http_add_userinfo(const char *s, struct curl_httppost** formpost, struct curl_httppost** lastptr) {
	char	pkey[BIG_INFO_KEY];
	static	char value[BIG_INFO_VALUE];
	char	*o;
	
	if ( !s ) {
		return;
	}

	if ( strlen( s ) >= BIG_INFO_STRING ) {
		return;
	}

	if (*s == '\\')
		s++;
	while (1)
	{
		o = pkey;
		while (*s != '\\')
		{
			if (!*s)
				return;
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value;

		while (*s != '\\' && *s)
		{
			*o++ = *s++;
		}
		*o = 0;

		curl_formadd(formpost, lastptr,
			CURLFORM_COPYNAME, QMM_VARARGS("info_%s", pkey),	//prefix the post key to prevent injection
			CURLFORM_COPYCONTENTS, value,
		CURLFORM_END);

		if (!*s)
			break;
		s++;
	}
}

char    *ConcatArgs( int start ) {
        int i, c, tlen;
        static char line[MAX_STRING_CHARS];
        int len;
        char arg[MAX_STRING_CHARS];

        len = 0;
        c = g_syscall(G_ARGC);
        for ( i = start ; i < c ; i++ ) {
                g_syscall(G_ARGV, i, arg, sizeof( arg ) );
                tlen = strlen( arg );
                if ( len + tlen >= MAX_STRING_CHARS - 1 ) {
                        break;
                }
                memcpy( line + len, arg, tlen );
                len += tlen;
                if ( i != c - 1 ) {
                        line[len] = ' ';
                        len++;
                }
        }

        line[len] = 0;

        return line;
}

char* skip_whitespace(char* str) {
	while(*str) {
		switch(*str) {
			case 0x09:
			case 0x0a:
			case 0x0b:
			case 0x0c:
			case 0x0d:
			case 0x20:
				str++;
				break;
			default:
				return str;
		}
	}
	
	return str;
}

http_t* http_make() {
	http_t *http = (http_t*)malloc(sizeof(http_t));
	memset(http, 0, sizeof(http_t));
	http->buffer = buffer_make();
	http->curlHandle = curl_easy_init();	
	curl_easy_setopt(http->curlHandle, CURLOPT_WRITEDATA, http);
	curl_easy_setopt(http->curlHandle, CURLOPT_WRITEFUNCTION, &http_write);
	
	return http;
}

void http_destroy(http_t** http) {
	http_t* h = *http;
	buffer_destroy(&h->buffer);
	curl_easy_cleanup(h->curlHandle);
	curl_formfree(h->post);
	free(h);
	*http = NULL;
}

void http_abort(http_t** http) {
	//http->abort = true;
	curl_multi_remove_handle(curl_handle, (*http)->curlHandle);
	http_destroy(http);
}

size_t http_write(char *ptr, size_t size, size_t nmemb, void *userdata) {
	size_t realSize = size * nmemb;
	http_t* http = (http_t*)userdata;
	
	return buffer_append(&http->buffer, ptr, realSize);
}

size_t buffer_append(buffer_t** buffer, char* ptr, size_t size) {
	(*buffer)->content = (char*)realloc((*buffer)->content, (*buffer)->size + size + 1);
	
	if(*buffer == NULL)
		return -1;
		
	memcpy((*buffer)->content + (*buffer)->size, ptr, size);
	(*buffer)->size += size;
	(*buffer)->content[(*buffer)->size] = 0;
	
	return size;
}

buffer_t* buffer_make() {
	buffer_t *b = (buffer_t*)malloc(sizeof(buffer_t));
	b->content = (char*)malloc(1);
	b->size = 0;
	
	return b;
}

buffer_t* buffer_copy(buffer_t* buffer) {
	buffer_t* newBuffer = buffer_make();
	newBuffer->size = buffer->size;
	newBuffer->content = (char*)malloc(newBuffer->size + 1);
	memcpy(newBuffer->content, buffer->content, newBuffer->size + 1);
	
	return newBuffer;
}

void buffer_destroy(buffer_t** b) {
	buffer_t* buffer = *b;
	free(buffer->content);
	free(buffer);
	*b = NULL;
}

void authenticator_init() {
	curl_handle = curl_multi_init();
	authenticator_queue_game();
}

void authenticator_shutdown() {
	curl_multi_cleanup(curl_handle);
	int max_clients = QMM_GETINTCVAR("sv_maxclients");
	for(int i = 0; i < max_clients; i++) {
		playerinfo_t *player = &g_playerinfo[i];
		if(player->request) {
			http_remove_transfer(player->request);
			http_destroy(&player->request);
		}
	}
}

void authenticator_process_queue() {
	CURLMsg *msg = NULL;
	int size = 0;
	
	while((msg = curl_multi_info_read(curl_handle, &size))) {
		if(msg->msg == CURLMSG_DONE) {
			char *data;
			curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &data);
			//g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Handling request %p\n", msg->easy_handle));

			if(data == NULL) {
				//assume this is a game request
				authenticator_handle_game();
			} else {
				//assume this is a player request
				authenticator_handle_player(msg, (playerinfo_t*)(void*)data);
			}
		} else {
			//this should never happen
		}
	}
}

void authenticator_handle_game() {
	authenticator_exec_body(game_http);
	http_destroy(&game_http);
}

void authenticator_handle_player(CURLMsg* msg, playerinfo_t* player) {
	CURL *easy = msg->easy_handle;
	
	CURLcode result = msg->data.result;
	
	//only process if this was the last request sent
	if(result == CURLE_OK && easy == player->request->curlHandle) {
		//assume request was successful
		long code;
		curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
		
		//authenticate first, then attempt to execute body
		player->authenticated = code == 200;
		if(player->authenticated) {
			cpm(player->clientNum, "^2[AUTH] Your identity has been confirmed.");
			g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Successfully authenticated client %d\n", player->clientNum));			
			g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Saving hash for authenticated player %d\n", player->clientNum));
			//this player is authenticated, save a hash over his data
			char hash[HASH_LENGTH];
			hash_calculate(player, hash, sizeof(hash));
			g_syscall(G_CVAR_SET, QMM_VARARGS(CVAR_HASH_FMT, player->clientNum), hash);
			
		} else {
			g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Failed to authenticate client %d\n", player->clientNum));
		}
		auth_user = player->clientNum;
		authenticator_exec_body(player->request);
		auth_user = -1;
	} else {
		g_syscall(G_PRINT, QMM_VARARGS("[AUTH] cURL error: %d\n", result));
	}
	
	//clean up
	http_destroy(&player->request);
}

void authenticator_exec_body(http_t* http) {
	if(http->buffer->size) {
		g_syscall(G_PRINT, "[AUTH] Executing body:\n");
		g_syscall(G_PRINT, http->buffer->content);
		g_syscall(G_PRINT, "\n");
		g_syscall(G_PRINT, "[AUTH] end body\n");
		
		//run this asynchronously, but add it to the front of the execution queue
		g_syscall(G_SEND_CONSOLE_COMMAND, EXEC_INSERT, QMM_VARARGS("%s\n", http->buffer->content));
	}
}

void authenticator_perform() {
	//TIMER_START
	if(active_transfers) {
		int still_running = 0;
		curl_multi_perform(curl_handle, &still_running);
		
		if(still_running < active_transfers) {
			//at least one transfer is finished
			active_transfers = still_running;
			authenticator_process_queue();
		}
	}
	//TIMER_END
	//TIMER_PRINT(authenticator_perform)
}

http_t* authenticator_prepare_request(const char* type) {
	http_t* request = http_make();
	curl_easy_setopt(request->curlHandle, CURLOPT_URL, AUTH_URL);
	
	curl_formadd(&request->post, &request->lastpost,	
		CURLFORM_COPYNAME, "type",
		CURLFORM_COPYCONTENTS, type,
		CURLFORM_END);
	
	//add some info about this server
	curl_formadd(&request->post, &request->lastpost,
		CURLFORM_COPYNAME, "api_key",
		CURLFORM_COPYCONTENTS, QMM_GETSTRCVAR(CVAR_API_KEY),
		CURLFORM_END);
		
	return request;
}
/*
void authenticator_queue_command(int cl, const char* command) {
	playerinfo_t *player = &g_playerinfo[cl];
	if(player->request != NULL) {
		cpm(cl, "^1Cannot run command at the moment!");
		return;
	}
	player->request = authenticator_prepare_request("command");

}
*/
void authenticator_queue_game() {
	game_http = authenticator_prepare_request("game");
	http_add_transfer(game_http);
}

void authenticator_queue_player(int cl, const char* userinfo) {
	playerinfo_t *player = &g_playerinfo[cl];
	
	// ensure that only one active request per player is used
	if(player->request != NULL) {
		//g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Cancelling inflight request for player %d\n", cl));
		http_abort(&player->request);
	}
	
	player->request = authenticator_prepare_request("player");
	if(player->request == NULL) {
		return;
	}
		
	CURL* curl = player->request->curlHandle;
	http_add_userinfo(userinfo, &player->request->post, &player->request->lastpost);	//add all userinfo to post 
	
	//add some custom data
	curl_formadd(&player->request->post, &player->request->lastpost,
		CURLFORM_COPYNAME, "cl",
		CURLFORM_COPYCONTENTS, QMM_VARARGS("%d", cl),
		CURLFORM_END);
		
	curl_formadd(&player->request->post, &player->request->lastpost,
		CURLFORM_COPYNAME, "level",
		CURLFORM_COPYCONTENTS, QMM_VARARGS("%d", player->level),
		CURLFORM_END);
	
	curl_easy_setopt(curl, CURLOPT_PRIVATE, player);
	http_add_transfer(player->request);
	g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Attempting to remotely authenticate client %d %p\n", cl, player->request->curlHandle));
}

void http_add_transfer(http_t* req) {
	curl_easy_setopt(req->curlHandle, CURLOPT_HTTPPOST, req->post);
	curl_multi_add_handle(curl_handle, req->curlHandle);
	active_transfers++;
}

void http_remove_transfer(http_t *req) {
	curl_multi_remove_handle(curl_handle, req->curlHandle);
	active_transfers--;
}

void userinfo_preload() {
	int max_clients = QMM_GETINTCVAR("sv_maxclients");
	for(int i = 0; i < max_clients; i++)
	{
		char userinfo[MAX_INFO_STRING];
		g_syscall(G_GET_USERINFO, i, userinfo, sizeof(userinfo));
		const char* hash = QMM_GETSTRCVAR(QMM_VARARGS(CVAR_HASH_FMT, i));
		
		//see if slot is occupied, ignore otherwise
		if(strlen(userinfo) > 0)
		{
			userinfo_store(i, userinfo);
			if(!hash_verify(i, hash)) {
				//we don't have a matching hash stored for this client
				//reauthenticate
				authenticator_queue_player(i, userinfo);
			}

		}
	}
}

void hash_transform(char* buffer, size_t len, char* mod) {
	size_t modLen = strlen(mod);
	for(int i = 0; i < modLen; i++) {
		for(int j = 0; j < len; j++) {
			int b = mod[i] * j ^ buffer[j] * i;
			b %= sizeof(hashChars) - 1;
			buffer[j] = hashChars[b];
		}
	}
}

void hash_calculate(playerinfo_t* player, char* buffer, size_t len) {
	hash_transform(buffer, len, player->ip);
	hash_transform(buffer, len, player->guid);
	hash_transform(buffer, len, player->mac);
	//null termination
	buffer[len - 1] = 0;
}

bool hash_verify(int cl, const char* hash) {
	playerinfo_t *player = &g_playerinfo[cl];
	if(strlen(hash)) {
		char newHash[HASH_LENGTH];
		hash_calculate(player, newHash, sizeof(newHash));
		
		//assume player is authenticated, if hashes match
		player->authenticated = strcmp(hash, newHash) == 0;
		if(!player->authenticated)
			g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Warning: client %d hash doesn't match (%s, %s)\n", cl, hash, newHash));
		else
			g_syscall(G_PRINT, QMM_VARARGS("[AUTH] client %d authenticated with hash %s\n", cl, hash));
	} else {
		g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Emtpy hash for client %d\n", cl));
		player->authenticated = false;
	}
	
	return player->authenticated;
}

void userinfo_store(int cl, char* userinfo) {
	strncpy(g_playerinfo[cl].ip, Info_ValueForKey(userinfo, "ip"), sizeof(g_playerinfo[cl].ip));
	char* temp = strstr(g_playerinfo[cl].ip, ":");
	if (temp) *temp = '\0';
	memset(&g_playerinfo[cl].guid, 0, sizeof(g_playerinfo[cl].guid));
	strncpy(g_playerinfo[cl].guid, Info_ValueForKey(userinfo, "cl_guid"), sizeof(g_playerinfo[cl].guid));
	memset(&g_playerinfo[cl].mac, 0, sizeof(g_playerinfo[cl].mac));
	strncpy(g_playerinfo[cl].mac, Info_ValueForKey(userinfo, "cl_mac"), sizeof(g_playerinfo[cl].mac));
	g_playerinfo[cl].clientNum = cl;
	g_playerinfo[cl].level = userinfo_get_level(cl);
	g_playerinfo[cl].connected = true;
}

int userinfo_get_level(int cl) {

	buffer_t* b = console_command(QMM_VARARGS("!finger %d\n", cl));
	char* tok = strtok(b->content, "\n");
	int count = 0;
	int level = 0;
	
	while(tok != NULL) {
		if(!strncmp(tok, "level:", 6)) {
			sscanf(tok, "level: %d", &level);			
			return level;
		}
	
		tok = strtok(NULL, "\n");
	}

	buffer_destroy(&b);
	
	return -1;
}

/**
	returns a buffer containing the captured console output.
	the caller needs to take care of destroying the buffer
*/
buffer_t* console_command(char* command) {
	console_buffer = buffer_make();	
	g_syscall(G_SEND_CONSOLE_COMMAND, EXEC_NOW, command);
	
	buffer_t* buffer = buffer_copy(console_buffer);
	buffer_destroy(&console_buffer);
	return buffer;
}

C_DLLEXPORT void QMM_Query(plugininfo_t** pinfo) {
	//give QMM our plugin info struct
	*pinfo = &g_plugininfo;	
}

C_DLLEXPORT int QMM_Attach(eng_syscall_t engfunc, mod_vmMain_t modfunc, pluginres_t* presult, pluginfuncs_t* pluginfuncs, int vmbase, int iscmd) {
	g_syscall = engfunc;
	g_vmMain = modfunc;
	g_result = presult;
	g_vmbase = vmbase;
	g_pluginfuncs = pluginfuncs;
	
	g_syscall(G_CVAR_REGISTER, NULL, "twc_authenticator",	"v" AUTH_QMM_VERSION, CVAR_ARCHIVE | CVAR_SERVERINFO | CVAR_ROM);
	g_syscall(G_CVAR_REGISTER, NULL, CVAR_URL, 				"0", CVAR_ARCHIVE);
	g_syscall(G_CVAR_REGISTER, NULL, CVAR_API_KEY,			"0", CVAR_ARCHIVE);
	int maxClients = QMM_GETINTCVAR("sv_maxclients");
	
	
	//create a hash for each client slot
	for(int i = 0; i < maxClients; i++) {
		g_syscall(G_CVAR_REGISTER, NULL, QMM_VARARGS(CVAR_HASH_FMT, i), "", CVAR_ARCHIVE);
	}
	
	if(iscmd) {
		authenticator_init();
		authenticator_need_preload = true;
	}
	
	return 1;
}

C_DLLEXPORT void QMM_Detach(int iscmd) {
	//ignore 'iscmd' but satisfy gcc -Wall
	if(iscmd) {
		authenticator_shutdown();
	}
}

C_DLLEXPORT int QMM_vmMain(int cmd, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11) {
	playerinfo_t* player;
	const char* hash;
	char userinfo[MAX_INFO_STRING];
	switch(cmd) {
		case GAME_CLIENT_DISCONNECT:
			//stop the transfer that might run for this player
			player = &g_playerinfo[arg0];
			if(player->request != NULL) {
				http_abort(&player->request);
			}
			memset(&g_playerinfo[arg0], 0, sizeof(g_playerinfo[arg0]));
			break;
		case GAME_INIT:
			authenticator_init();
			break;
		case GAME_SHUTDOWN:
			authenticator_shutdown();
			break;
		case GAME_CLIENT_USERINFO_CHANGED: {
			if (!g_playerinfo[arg0].connected)
				break;
		
			char userinfo[MAX_INFO_STRING];
			g_syscall(G_GET_USERINFO, arg0, userinfo, sizeof(userinfo));

			if (!Info_Validate(userinfo))
				QMM_RET_IGNORED(1);

			char guid[MAX_GUID_LENGTH];
			strncpy(guid, Info_ValueForKey(userinfo, "cl_guid"), sizeof(guid));
			
			if(strncmp(guid, g_playerinfo[arg0].guid, sizeof(guid)))
			{
				g_syscall(G_PRINT, QMM_VARARGS("[AUTH] client %d changed GUID %s -> %s\n", arg0, g_playerinfo[arg0].guid, guid));
				cpm(arg0, QMM_VARARGS("^1[AUTH] Your GUID changed, you will be re-authenticated!"));
				strncpy(g_playerinfo[arg0].guid, guid, sizeof(g_playerinfo[arg0].guid));
				g_playerinfo[arg0].authenticated = 0;
				authenticator_queue_player(arg0, userinfo);
				QMM_RET_SUPERCEDE(1);
			}
			break;
		}
		case GAME_CLIENT_COMMAND: {
			char tempbuf[128];
			g_syscall(G_ARGV, 0, tempbuf, sizeof(tempbuf) - 1);
			int argc = g_syscall(G_ARGC);
			int start;
			
			if(!strcasecmp(tempbuf, "auth_info")) {
				player = &g_playerinfo[arg0];
				cpm(arg0, QMM_VARARGS("IP:    %s", player->ip));
				cpm(arg0, QMM_VARARGS("GUID:  %s", player->guid));
				cpm(arg0, QMM_VARARGS("MAC:   %s", player->mac));
				cpm(arg0, QMM_VARARGS("Level: %d", player->level));
				cpm(arg0, QMM_VARARGS("Auth:  %d", player->authenticated));
				
				QMM_RET_SUPERCEDE(1);
			} else if(!strcasecmp(tempbuf, "authenticate")) {
				player = &g_playerinfo[arg0];
				if(player->authenticated) {
					cpm(arg0, "^3You are authorized already");
				} else {
					g_syscall(G_GET_USERINFO, arg0, userinfo, sizeof(userinfo));
					authenticator_queue_player(arg0, userinfo);					
				}
				
				QMM_RET_SUPERCEDE(1);
			}
			/*
			else if(!strncasecmp(tempbuf, "remote", 6)) {
				authenticator_queue_command(arg0, tempbuf + 6);
				QMM_RET_SUPERCEDE(1);
			}
			*/
			// g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Scanning for command, argc=%d\n", argc));
			
			// skip all leading arguments that start with "say"
			for(start = 0; !strncasecmp(tempbuf, "say", 3) && start < argc; g_syscall(G_ARGV, ++start, tempbuf, sizeof(tempbuf))) {
				// g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Skipping argument %d = \"%s\"\n", start, tempbuf));
			}
			
			char *command = ConcatArgs(start);
			command = skip_whitespace(command);
			
			// g_syscall(G_PRINT, QMM_VARARGS("[AUTH] Checking command: %s\n", command));
			
			if(*command == '!' && !g_playerinfo[arg0].authenticated) {
				g_syscall(G_PRINT,
					QMM_VARARGS("[AUTH] unauthorized client %d attempting to send command: %s\n", arg0, command));
				cpm(arg0, "^1You are not authorized to use !commands");
				QMM_RET_SUPERCEDE(1);
			}
			
			if(!strncasecmp(command, "!ab", 3)) {
				QMM_RET_SUPERCEDE(1);
			}
			
			break;
		}
		case GAME_CONSOLE_COMMAND:
			char tempbuf[128];
			g_syscall(G_ARGV, 0, tempbuf, sizeof(tempbuf));
			if (!strcasecmp(tempbuf, "auth_error")) {
				if(auth_user < 0)
					QMM_RET_IGNORED(0);
				
				char* message = QMM_VARARGS("^1[AUTH] Error: %s\n", ConcatArgs(1));
				cpm(auth_user, message);
				QMM_RET_SUPERCEDE(1);
			}
			
			
			// enhanced mod feels like blocking clientkick, so we implement it ourselves ( ° ͜ʖ͡° )╭∩╮
			if(!strncasecmp(tempbuf, "clientkick", 10) || !strncasecmp(tempbuf, "kick", 4))
			{
				char reason[64];
				g_syscall(G_ARGV, 1, reason, sizeof(reason));
				int cl = atoi(reason);
				
				if(cl < 0 || cl > QMM_GETINTCVAR("sv_maxclients"))
				{
					g_syscall(G_PRINT, QMM_VARARGS("Error: client number has to be between 0 and %i", QMM_GETINTCVAR("sv_maxclients")));
				}
				else
				{				
					g_syscall(G_DROP_CLIENT, cl, "you have been kicked", 0);
				}
				
				QMM_RET_SUPERCEDE(1);
			}
			break;
		case GAME_RUN_FRAME:
			if(authenticator_need_preload) {
				authenticator_need_preload = false;
				userinfo_preload();
			}
			authenticator_perform();
			break;
	}
	QMM_RET_IGNORED(1);
}

C_DLLEXPORT int QMM_syscall(int cmd, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11, int arg12) {
	switch(cmd) {
		case G_LOCATE_GAME_DATA: {
			g_gents = (gentity_t*)arg0;
			//arg1 is number of entities
			g_gentsize = arg2;
			g_clients = (gclient_t*)arg3;
			g_clientsize = arg4;
		}
			break;
		
		case G_PRINT:
			//catch output of rcon commands here, if needed
			if(console_buffer == NULL)
				break;
			
			buffer_append(&console_buffer, (char*)arg0, strlen((char*)arg0));
			QMM_RET_SUPERCEDE(0);
			break;
	}

	QMM_RET_IGNORED(1);
}

C_DLLEXPORT int QMM_syscall_Post(int cmd, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11, int arg12) {
	QMM_RET_IGNORED(1);
}

C_DLLEXPORT int QMM_vmMain_Post(int cmd, int arg0, int arg1, int arg2, int arg3, int arg4, int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11) {
	const char* hash;
	char userinfo[MAX_INFO_STRING];
	switch(cmd) {
		case GAME_CLIENT_BEGIN:
			//memset(&g_playerinfo[arg0], 0, sizeof(g_playerinfo[arg0]));
			g_syscall(G_GET_USERINFO, arg0, userinfo, sizeof(userinfo));

			if (!Info_Validate(userinfo)) {
				QMM_RET_IGNORED(1);
			}
			
			userinfo_store(arg0, userinfo);
			hash = QMM_GETSTRCVAR(QMM_VARARGS(CVAR_HASH_FMT, arg0));
			if(!hash_verify(arg0, hash)) {
				//we don't have a matching has stored for this client
				//reauthenticate
				authenticator_queue_player(arg0, userinfo);
			}

			break;
	}

	QMM_RET_IGNORED(1);
}
