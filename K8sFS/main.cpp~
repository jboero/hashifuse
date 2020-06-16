/****************************************************************************
**
** k8sFS - FUSE2 client for Kubernetes manifests.
**
** Authored by John Boero
** Build instructions: g++ -D_FILE_OFFSET_BITS=64 -lfuse -lcurl -ljsoncpp main.cpp
** Usage: ./k8sfs -o direct_io /path/to/mount
**
** Note direct_io is mandatory right now until we can get key size in getattrs.
** Note this is currently a highly experimental draft.  Reads should be ok.
**	Writes are much trickier as Kube API is not very idempotent-friendly.
**	Reading an endpoint gives extra attributes that often can't be written back,
**	for example - read gets a timestamp.  write can't apply a timestamp, etc.
** 
** Environment Variables: 
	KUBE_APISERVER		k8s addr.  Example: "https://localhost:4646"
	K8SFS_LOG			optional log file path.

	KUBE_TOKEN			optional k8s token for auth. (Token auth)
	K8SFS_CA_PEM		optional manual CA PEM for libcurl (Cert auth)
	K8SFS_CLIENT_CERT	optional manual client PEM (client cert + key)
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
#include <regex>
#include <exception>

#include <fuse.h>

using namespace std;

// Set logs to other options via K8SFS_LOG or default to std::cout
ostream *logs = &cout;

// Protect multi-threaded mode from libcurl/libopenssl race condition.
mutex curlmutex;

// Term colors for stdout
const char RESET[]	= "\033[0m";
const char RED[]	= "\033[1;31m";
const char YELLOW[]	= "\e[0;33m";
const char CYAN[]	= "\e[1;36m";
const char PURPLE[]	= "\e[0;35m";
const char GREEN[]	= "\e[0;32m";
const char BLUE[]	= "\e[0;34m";

// Keep a global set of placeholder files we've created locally.
set<string> createds;

// CURL callback
namespace
{
    std::size_t write_callback(
            const char* in,
            std::size_t size,
            std::size_t num,
            char* out)
    {
    	if (!out)
    		return 0;
    	
        string data(in, (size_t) size * num);
        *((stringstream*) out) << data;

        #if DEBUG
		*logs << PURPLE << data << RESET << endl;
		#endif
        return size * num;
    }
}

// Easy libcurl
// Currently supports request GET (default), PUT, LIST, DELETE
// TODO: sanitize environment variables for injection vulnerabilities.
int	k8sCURL(string url, stringstream *httpData = NULL, string request = "GET", const string data = "")
{
	int httpCode = 0;
	const char *addr = getenv("KUBE_APISERVER");
	const char *token = getenv("KUBE_TOKEN");
	static const string tokenHead = "Authorization: Bearer ";
	struct curl_slist *headers = NULL;
	CURL* c;

	url = addr ? (string)addr + url : "http://localhost:8080" + url;
	
	#if DEBUG
	*logs << CYAN << url << RESET << endl;
	#endif

	// Multi-thread mode is a race condition mine field, so we'll just lock here.
	// Not an issue when single-threaded.  I spent hours on this and this line seems the best fix.
	// Destructor of lk will release this mutex in any case. Could probably use shared curl.
	{
		lock_guard<mutex> lk(curlmutex); // DON'T move this -- the race condition gods
		if (!(c = curl_easy_init()))
			return -1;
		
		// Beware error handling (lack).
		if (token)
			headers = curl_slist_append(headers, (tokenHead + token).c_str());
		
		// Note the ENV variable curl standardizes on has no effect sadly.
		// TODO: Robustify ca bundle...
		if (getenv("K8SFS_CA_PEM"))
			curl_easy_setopt(c, CURLOPT_CAINFO, getenv("K8SFS_CA_PEM"));
		if (getenv("K8SFS_CLIENT_CERT"))
			curl_easy_setopt(c, CURLOPT_SSLCERT, getenv("K8SFS_CLIENT_CERT"));
		
		if (httpData)
			curl_easy_setopt(c, CURLOPT_WRITEDATA, httpData);

		if (data != "")
		{
			headers = curl_slist_append(headers, "Content-Type: application/json");
//			headers = curl_slist_append(headers, "Accept: application/json;as=Table;g=meta.k8s.io;v=v1beta1");
//			headers = curl_slist_append(headers, "Accept: application/json");
			curl_easy_setopt(c, CURLOPT_POSTFIELDS, data.c_str());
			#if DEBUG
			*logs << GREEN << data << RESET << endl;
			#endif 
		}

		curl_easy_setopt(c, CURLOPT_URL, url.c_str());
		curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, request.c_str());
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
		curl_easy_setopt(c, CURLOPT_TIMEOUT, 1);
		curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_callback);

		curl_easy_perform(c);

		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &httpCode);
		curl_easy_cleanup(c);
		curl_slist_free_all(headers);
	}

	// libCurl has a surprise 0 response code sometimes...
	if (httpCode < 200 || httpCode >= 300)
	{
		*logs << RED << "Couldn't " << request << " " << data << " -> " << url << " HTTP" << httpCode << RESET << endl;
		return httpCode ? httpCode : -2;
	}

	return 0;
}

// CURL wrapper with JSON
int	k8sCURLjson(string url, Json::Value &jsonData, string request = "GET", string post = "")
{
	stringstream stream;
	if (k8sCURL(url, &stream, request, post))
		return -EINVAL;

	stream >> jsonData;
	return 0;
}

// Helper to translate fs path to correct REST path.
// Your mileage may vary based on K8s release...
string getRESTbase(string fspath)
{
	if (regex_match(fspath, (regex)"^/(.*)/(daemonsets|deployments|replicasets)(.*)$"))
		return "/apis/apps/v1/namespaces";
	else if (regex_match(fspath, (regex)"^/(.*)/(cronjobs)$"))
		return "/apis/batch/v1beta1/namespaces";
	else if (regex_match(fspath, (regex)"^/(.*)/(jobs)$"))
		return "/apis/batch/v1/namespaces";
	return "/api/v1/namespaces";
}

// We need to assume quite a few attrs.
int k8s_getattr(const char *path, struct stat *stat)
{
	string p(path), key;
	size_t depth = count(p.begin(), p.end(), '/');

	// Remove optional ".json" suffix we added in readdir
	size_t suffix = p.find(".json");
	if (suffix != string::npos)
		p.erase(suffix, 5);

	stat->st_uid = getuid();
	stat->st_gid = getgid();
	stat->st_blocks = 
	stat->st_blksize = 
	stat->st_size = 0;

	// Be careful with timestamp - file will always appear modified on disk.
	// If using rsync, disable timestamp comparisons.
	//stat->st_atime = stat->st_mtime = stat->st_ctime = 0;
	stat->st_atime = stat->st_mtime = stat->st_ctime = time(NULL);

	// Are we 1 or 2 levels deep?  Just dirs.
	if (depth <= 2)
		stat->st_mode = S_IFDIR | 0700;	// Need 7 for rsync :/
	else
		stat->st_mode = S_IFREG | 0600;

	return 0;
}

// Read ops seem fairly simple, but as we need direct_io and can't guess size, 2 reads are necessary.
// It would be more efficient to read in the entire buffer in OPEN, and free it in RELEASE, but this works.
// Read once, fetch.  Read again to verify 0 bytes left.  This won't scale with latency.
int k8s_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	string data, p(path);
	stringstream sstream;

	// Is this a placeholder we've created locally?
	if (createds.find(path) != createds.end())
		return 0;
	
	// Remove optional ".json" suffix we added in readdir
	size_t suffix = p.find(".json");
	if (suffix != string::npos)
		p.erase(suffix, 5);

	// TODO adapt this for different types
	if (k8sCURL(getRESTbase(p) + p + "?pretty=true", &sstream))
		return -ENOENT;
	
	data = sstream.str();
	if (data.length() - offset <= 0)
		return 0;

	strncpy(buf + offset, data.c_str(), data.length());
	return data.length();
}

// Writes are straightforward.  Should verify size < k8s maximum though the API should do that.
int k8s_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	stringstream sstream;
	string p(path), rest(getRESTbase(path));
	int httpCode;

	// Remove optional ".json" suffix we added in readdir
	size_t suffix = p.find(".json");
	if (suffix != string::npos)
		p.erase(suffix, 5);
	
	if ((httpCode = k8sCURL(rest + p, &sstream, "PUT", buf)))
	{
		// 400 means we'll need to create this.
		if (httpCode == 400)
			p = p.substr(0, p.find_last_of('/'));
		
		if (k8sCURL(rest + p, &sstream, "POST", buf))
			return -EINVAL;
	}

	// Remove placeholder if we successfully wrote it to API
	if (createds.find(path) != createds.end())
		createds.erase(path);
	
	return size;
}

// List directory contents.
int k8s_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	Json::Value result;
	string p(path), basepath(getRESTbase(path));
	size_t depth = count(p.begin(), p.end(), '/');

	if (p == "/")
		p = "";
	else if (depth == 1)
	{
		filler(buf, "pods", NULL, 0);
		filler(buf, "services", NULL, 0);
		filler(buf, "deployments", NULL, 0);
		filler(buf, "daemonsets", NULL, 0);
		filler(buf, "replicasets", NULL, 0);
		filler(buf, "cronjobs", NULL, 0);
		filler(buf, "jobs", NULL, 0);
		return 0;
	}

	if (k8sCURLjson(basepath + p, result))
		return -EINVAL;
	
	// TODO better check for errors before this.
	result = result["items"];
	for (Json::Value::const_iterator itr = result.begin() ; itr != result.end() ; itr++ )
	{
		string name = (*itr)["metadata"]["name"].asString();
		if (depth > 1)
			name += ".json";

		filler(buf, name.c_str(), NULL, 0);
	}

	return 0;
}

// Need to implement this for truncate/write even though we do nothing.
int k8s_truncate(const char *path, off_t newsize)
{
	return 0;
}

// Write a blank key
int k8s_mkdir(const char *path, mode_t mode)
{
	return k8s_write(((string)path + '/').c_str(), "", 0, 0, NULL);
}

int k8s_unlink(const char *path)
{
	stringstream stream;
	string rpath = getRESTbase(path) + path;

	// Need to include any string in data to specify JSON
	switch (k8sCURL(rpath, &stream, "DELETE", "{}"))
	{
		case 404:	return -ENOENT;
		case 403:	return -EPERM;
	}
	return 0;
}

int k8s_chmod(const char *path, mode_t mode)
{
	return 0;
}

// Return stat of root fs (partition).
// We need to show some space available on device to create anything new...
int k8s_statfs(const char *path, struct statvfs *statv)
{
	statv->f_bsize	= 
	statv->f_frsize	= 
	statv->f_blocks	= 
	statv->f_bfree	= 
	statv->f_bavail	= 32768;
	statv->f_files	= 
	statv->f_bfree	= 15;
	statv->f_favail	= 10000;
	statv->f_fsid	= 100;
	statv->f_flag	= 0;
	statv->f_namemax = 0xFFFF;
	return 0;
}

// Init curl subsystem and set up log stream.
void* k8s_init(struct fuse_conn_info *conn)
{
	curl_global_init(CURL_GLOBAL_ALL);

	// Default to cout, which is ignored without -d or -f arg.
	if (getenv("KUBEFS_LOG"))
	{
		if (!(logs = new ofstream(getenv("KUBEFS_LOG"), ofstream::out)))
		{
			cerr << RED << "Unable to open log output file for writing: " << getenv("K8SFS_LOG") << endl;
			cerr << "Will revert back to std::cout" << RESET << endl;
			logs = &cout;
		}
	}

	// Always big writes... 4k may not be enough.
	conn->want |= FUSE_CAP_BIG_WRITES;

	return NULL;
}

// Free up curl resources.
void k8s_destroy(void* private_data)
{
	curl_global_cleanup();
}

// Create must add a placeholder so we can get gettatr and read (0).
int k8s_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	// Use a local placeholder.
	createds.insert(path);
	return 0;
}


// Token renewal background thread.
void *renew_token (void *ignored)
{
	int delay;

	try
	{
		delay = atoi(getenv("KUBE_RENEW_TTL"));
	}
	catch (exception &e)
	{
		cerr << "Can't get KUBE_RENEW_TTL, defaulting to 60 seconds." << endl;
		delay = 60;
	}
	
	while (true)
	{
		// Examples: 
		// export KUBE_TOKEN=$(/usr/lib64/google-cloud-sdk/bin/gcloud 
		//	config config-helper --format=json | jq -r .credential.access_token)

		// vault lease renew [my-k8s-lease-id]
		//system(getenv("KUBE_RENEW_TOKEN"));
		sleep (delay);
	}
}

// Set up function pointers and return a fuse_operations struct.
int main(int argc, char *argv[])
{
	struct fuse_operations fuse = 
	{
		.getattr = k8s_getattr,
		.mkdir = k8s_mkdir,
		.unlink = k8s_unlink,
		.chmod = k8s_chmod,
		.truncate = k8s_truncate,
		.read = k8s_read,
		.write = k8s_write,
		.statfs = k8s_statfs,
		.readdir = k8s_readdir,
		.init = k8s_init,
		.destroy = k8s_destroy,
		.create = k8s_create
	};

	if ((getuid() == 0) || (geteuid() == 0))
		cerr << YELLOW << "WARNING Running a FUSE filesystem as root opens security holes" << endl;
	
	pthread_t renewer;
	if (getenv("KUBE_RENEW_TOKEN"))
	{
		pthread_create(&renewer, NULL, renew_token, NULL);
		pthread_detach(renewer);
	}

	return fuse_main(argc, argv, &fuse, NULL);
}
