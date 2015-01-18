#include <curl/curl.h>

#define cpm(t, x) g_syscall(G_SEND_SERVER_COMMAND, t, QMM_VARARGS("cpm \"%s\" 1",x))
#define cp(t,p) g_syscall(G_SEND_SERVER_COMMAND, t, QMM_VARARGS("cp \"%s\"", p))

#define CVAR_URL				"sv_auth_url"
#define CVAR_API_KEY			"sv_auth_api_key"
#define CVAR_HASH_FMT			"sv_auth_hash_%d"

#define CVAR_TRUE(v)		(QMM_GETINTCVAR(v) == 1)

#define HASH_LENGTH 16 + 1

#define MAX_GUID_LENGTH 32 + 1
#define MAX_IP_LENGTH 15 + 1
#define MAX_MAC_LENGTH 17 + 1

gentity_t* g_gents = NULL;
int g_gentsize = sizeof(gentity_t);
gclient_t* g_clients = NULL;
int g_clientsize = sizeof(gclient_t);

typedef struct buffer_s {
	char* content;
	size_t size;
} buffer_t;

typedef struct http_s {
	buffer_t* buffer;
	CURL* curlHandle;
	struct curl_httppost* post;
	struct curl_httppost* lastpost;
} http_t;

typedef struct playerinfo_s {
	char guid[MAX_GUID_LENGTH];
	char ip[MAX_IP_LENGTH];
	char mac[MAX_MAC_LENGTH];
	bool authenticated;
	http_t* request;
	int clientNum;
	int level;
	bool connected;
} playerinfo_t;


extern playerinfo_t g_playerinfo[];

void userinfo_store(int, char* );
void userinfo_preload();
int userinfo_get_level(int);

void hash_transform(char*, size_t, char*);
void hash_calculate(playerinfo_t*, char*, size_t);
bool hash_verify(int, const char*);

http_t* http_make();
void http_free(http_t*);
size_t http_write(char *, size_t, size_t, void *);
void http_add_transfer(http_t *);
void http_remove_transfer(http_t *);
void http_add_userinfo(const char *, struct curl_httppost**, struct curl_httppost**);

buffer_t* buffer_make();
size_t buffer_append(buffer_t**, char*, size_t);
void buffer_destroy(buffer_t**);

void authenticator_init();
void authenticator_shutdown();
void authenticator_perform();
void authenticator_process_queue();
void authenticator_handle_game();
void authenticator_handle_player(CURLMsg*, playerinfo_t*);
void authenticator_exec_body(http_t*);
void authenticator_queue_game();
void authenticator_queue_player();
http_t* authenticator_prepare_request(const char*);

buffer_t* console_command(char *);

qboolean Info_Validate( const char *s ) {
	if ( strchr( s, '\"' ) ) {
		return qfalse;
	}
	if ( strchr( s, ';' ) ) {
		return qfalse;
	}
	return qtrue;
}

int Q_stricmpn (const char *s1, const char *s2, int n) {
	int		c1, c2;
	
	do {
		c1 = *s1++;
		c2 = *s2++;

		if (!n--) {
			return 0;		// strings are equal until end point
		}
		
		if (c1 != c2) {
			if (c1 >= 'a' && c1 <= 'z') {
				c1 -= ('a' - 'A');
			}
			if (c2 >= 'a' && c2 <= 'z') {
				c2 -= ('a' - 'A');
			}
			if (c1 != c2) {
				return c1 < c2 ? -1 : 1;
			}
		}
	} while (c1);
	
	return 0;		// strings are equal
}

int Q_stricmp (const char *s1, const char *s2) {
	return (s1 && s2) ? Q_stricmpn (s1, s2, 99999) : -1;
}

char *Info_ValueForKey( const char *s, const char *key ) {
	char	pkey[BIG_INFO_KEY];
	static	char value[2][BIG_INFO_VALUE];	// use two buffers so compares
											// work without stomping on each other
	static	int	valueindex = 0;
	char	*o;
	
	if ( !s || !key ) {
		return "";
	}

	if ( strlen( s ) >= BIG_INFO_STRING ) {
		g_syscall( G_PRINT, "Info_ValueForKey: oversize infostring" );
	}

	valueindex ^= 1;
	if (*s == '\\')
		s++;
	while (1)
	{
		o = pkey;
		while (*s != '\\')
		{
			if (!*s)
				return "";
			*o++ = *s++;
		}
		*o = 0;
		s++;

		o = value[valueindex];

		while (*s != '\\' && *s)
		{
			*o++ = *s++;
		}
		*o = 0;

		if (!Q_stricmp (key, pkey) )
			return value[valueindex];

		if (!*s)
			break;
		s++;
	}

	return "";
}