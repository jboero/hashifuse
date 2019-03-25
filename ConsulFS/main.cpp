/****************************************************************************
**
** ConsulFS - FUSE client for Hashicorp consul secrets.
**
** Authored by John Boero
** Build instructions: g++ -D_FILE_OFFSET_BITS=64 -lfuse -lcurl -ljsoncpp main.cpp
** Usage: ./consulfs -o direct_io /path/to/mount
**
** Note direct_io is mandatory right now until we can get key size in getattrs.
** Environment Variables: 
	CONSUL_HTTP_ADDR		consul addr.  Example: "localhost:8500"
	CONSUL_HTTP_SSL[=true]	should we add "https://" to CONSUL_HTTP_ADDR? default false
	CONSUL_HTTP_TOKEN		token to auth via (token is only support currently)
	CONSULFS_LOG			path to file for logging output (or cout default)
	CONSULFS_DC				optional dc (nonstandard env variable)
****************************************************************************/

#define FUSE_USE_VERSION 28
//#define CURL_STATICLIB
//#define _GNU_SOURCE

#include <string>
#include <string.h>
#include <sstream>
#include <set>
#include <iostream>
#include <curl/curl.h>
#include <json/json.h>
#include <unistd.h>
#include <fstream>
#include <mutex>

#include "StdColors.h"
#include <fuse.h>

using namespace std;

// Global api version is static.
const string apiVers = "/v1";

// Will use this to store optional dc; This is deferred.
//string dc;

// Set logs to other options via CONSULFS_LOG or default to std::cout
ostream *logs = &cout;

// Protect multi-threaded mode from libcurl/libopenssl race condition.
mutex curlmutex;

// CURL callback
namespace
{
    std::size_t write_callback(
            const char* in,
            std::size_t size,
            std::size_t num,
            char* out)
    {
        string data(in, (size_t) size * num);
        *((stringstream*) out) << data;

        #if DEBUG
		*logs << GREEN << data << RESET << endl;
		#endif
        return size * num;
    }
}

// Easy libcurl
// Currently supports request GET (default), PUT, LIST, DELETE
// TODO: sanitize environment variables for injection vulnerabilities.
int	consulCURL(string url, stringstream &httpData, string request = "GET", const string data = "")
{
	int httpCode = 0;
	static const string tokenHead = "X-Consul-Token: ";
	string https = "";
	struct curl_slist *headers = NULL;
	CURL* curl;

	// Note case sensitivity.  Must be "true" lower for effect, or set https in CONSUL_HTTP_ADDR.
	if ((string)getenv("CONSUL_HTTP_SSL") == "true")
		https = "https://";
	
	url = https + getenv("CONSUL_HTTP_ADDR") + url;	// + dc;

	#if DEBUG
	*logs << CYAN << url << RESET << endl;
	#endif

	// Multi-thread mode is a race condition mine field, so we'll just lock on line 1...
	// Not an issue when single-threaded.  I spent hours on this and this line seems the best fix.
	// Destructor of lk will release this mutex in any case.
	{
		lock_guard<mutex> lk(curlmutex); // DON'T move this -- the race condition gods
		if (!(curl = curl_easy_init()))
			return -1;
		
		// Beware error handling (lack).
		headers = curl_slist_append(headers, (tokenHead + getenv("CONSUL_HTTP_TOKEN")).c_str());
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.c_str());
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &httpData);

		if (data != "")
		{
			#if DEBUG
			*logs << YELLOW << data << RESET << endl;
			#endif 
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
		}
		
		curl_easy_perform(curl);

		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
		curl_easy_cleanup(curl);
		curl_slist_free_all(headers);
	}
	if (httpCode < 200 || httpCode >= 300)
	{
		*logs << "Couldn't " << request << " " << data << " -> " << url << " HTTP" << httpCode << endl;
		return httpCode;
	}

	return 0;
}

// CURL wrapper with JSON
int	consulCURLjson(string url, Json::Value &jsonData, string request = "GET", string post = "")
{
	stringstream stream;
	if (consulCURL(url, stream, request, post))
		return -EINVAL;

	stream >> jsonData;
	return 0;
}

// We need to assume quite a few attrs.
// Use key trailing slash to identify dir/file.
int consul_getattr(const char *path, struct stat *stat)
{
	string p(path), key;
	Json::Value keys;

	stat->st_uid = getuid();
	stat->st_gid = getgid();
	stat->st_blocks = 
	stat->st_blksize = 
	stat->st_size = 0;

	// Be careful with timestamp - file will always appear modified on disk.
	// If using rsync, disable timestamp comparisons.
	//stat->st_atime = stat->st_mtime = stat->st_ctime = time(NULL);
	stat->st_atime = stat->st_mtime = stat->st_ctime = 0;

	// For now we just support kv endpoint.
	// TODO - add catalog, services, health, etc.
	if (p == "/" || p == "/kv")
	{
		stat->st_mode = S_IFDIR | 0500;
		return 0;
	}

	if (consulCURLjson(apiVers + p + "?keys&separator=/", keys))
		return -ENOENT;

	// Chop off the "/kv/"
	p = p.substr(4);
	for (Json::ValueConstIterator it = keys.begin(); it != keys.end(); ++it)
	{
		// Do keys start with "path" or "path/"?
		// Double search here is ultimate laziness but it works..
		key = it->asString();
		size_t plen = key.find(p);
		if (plen != string::npos)
		{
			if (key.length() > plen + p.length() && key[plen + p.length() ] == '/')
				stat->st_mode = S_IFDIR | 0700;
			else
				stat->st_mode = S_IFREG | 0600;
			return 0;
		}
	}

	return 0;
}

// Read ops seem fairly simple, but as we need direct_io and can't guess size, 2 reads are necessary.
// It would be more efficient to read in the entire buffer in OPEN, and free it in RELEASE, but this works.
// Read once, fetch.  Read again to verify 0 bytes left.  This won't scale with latency.
int consul_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	string data;
	stringstream sstream;

	if (consulCURL(apiVers + path + "?raw=true", sstream))
		return -ENOENT;

	data = sstream.str();
	if (data.length() - offset <= 0)
		return 0;

	strncpy(buf + offset, data.c_str(), data.length());
	return data.length();
}

// Writes are straightforward.  Should verify size < consul maximum though the API should do that.
int consul_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	stringstream stream;
	if (consulCURL(apiVers + path, stream, "PUT", buf))
		return -EINVAL;
	return size;
}

// Return stat of root fs (partition).
int consul_statfs(const char *path, struct statvfs *statv)
{
	statv->f_bsize	= 
	statv->f_frsize	= 
	statv->f_blocks	= 
	statv->f_bfree	= 
	statv->f_bavail	= 32768;
	statv->f_files	= 15;
	statv->f_bfree	= 15;
	statv->f_favail	= 10000;
	statv->f_fsid	= 100;
	statv->f_flag	= 0;
	statv->f_namemax = 0xFFFF;
	return 0;
}

// List directory contents of a Path.
int consul_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	Json::Value keys;
	string p(path), f;
	set<string> names;

	if (p == "/")
	{
		if (consulCURLjson(apiVers + p + "/catalog/datacenters", keys))
			return -ENOENT;
		
		filler(buf, "health", NULL, 0);


		return 0;
	}

	// Need separator to not recurse
	if (consulCURLjson(apiVers + p + "/?keys=true&separator=/", keys))
		return -ENOENT;

	// Chop off "/kv/" or "/kv" (annoyingly we need both).
	p = p.substr(3);
	if (p.length() > 0 && p[0] == '/')
		p = p.substr(1);
	
	// Use a set to eliminate duplicates.
	// Ugly but unfortunately Consul API keys=true implies recursive.
	for(Json::Value::const_iterator itr = keys.begin() ; itr != keys.end() ; itr++ )
	{
		size_t start = (p == "") ? 0 : p.length() + 1;
		f = itr->asString().substr(start);
		if (f == "")
			continue; // Self

		size_t slash = f.find('/');
		if (slash <= f.length() - 1)
			names.insert(f.substr(0, slash));
		else if (slash == string::npos)
			names.insert(f);
	}

	// Unwind the unique set..
	for (set<string>::iterator name = names.begin(); name != names.end(); ++name)
    	filler(buf, name->c_str(), NULL, 0);
	return 0;
}

// Need to implement this for truncate/write even though we do nothing.
int consul_truncate(const char *path, off_t newsize)
{
	return 0;
}

// Write a blank key
int consul_mkdir(const char *path, mode_t mode)
{
	return consul_write(((string)path + '/').c_str(), "", 0, 0, NULL);
}

// Write a blank key.
int consul_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	return consul_write(path, "", 0, 0, fi);
}

// rm file
int consul_unlink(const char *path)
{
	stringstream stream;
	if (consulCURL(apiVers + path, stream, "DELETE"))
		return -EINVAL;
	return 0;
}

// rm dir
int consul_rmdir(const char *path)
{
	return consul_unlink(((string)path + '/').c_str());
}

// Init curl subsystem and set up log stream.
void* consul_init(struct fuse_conn_info *conn)
{
	curl_global_init(CURL_GLOBAL_ALL);

	// Set CONSULFS_LOGS env var to log destination if necessary.
	// Default to cout, which is ignored without -d or -f arg.
	if (getenv("CONSULFS_LOG"))
	{
		if (!(logs = new ofstream(getenv("CONSULFS_LOG"), ofstream::out)))
		{
			cerr << RED << "Unable to open log output file for writing: " << getenv("CONSULFS_LOG") << endl;
			cerr << "Will revert back to std::cout" << RESET << endl;
			logs = &cout;
		}
	}

	// Set dc global if we need to.
	//if (getenv("CONSULFS_DC"))
	//	dc = (string)"dc=" + getenv("CONSULFS_DC");

	// TODO check/sanitize env variables for injection.
	conn->want |= FUSE_CAP_BIG_WRITES;

	return NULL;
}

// Free up curl resources.
void consul_destroy(void* private_data)
{
	curl_global_cleanup();
}

// Set up function pointers and return a fuse_operations struct.
int main(int argc, char *argv[])
{
	struct fuse_operations fuse = 
	{
		.getattr = consul_getattr,
		.mkdir = consul_mkdir,
		.unlink = consul_unlink,
		.rmdir = consul_rmdir,
		.truncate = consul_truncate,
		.read = consul_read,
		.write = consul_write,
		.statfs = consul_statfs,
		.readdir = consul_readdir,
		.init = consul_init,
		.destroy = consul_destroy,
		.create = consul_create,
	};

	if ((getuid() == 0) || (geteuid() == 0))
		cerr << YELLOW << "WARNING Running a FUSE filesystem as root opens security holes" << endl;
	
	return fuse_main(argc, argv, &fuse, NULL);
}
