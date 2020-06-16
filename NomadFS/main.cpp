/****************************************************************************
**
** NomadFS - FUSE client for Hashicorp nomad secrets.
**
** Authored by John Boero
** Build instructions: g++ -D_FILE_OFFSET_BITS=64 -lfuse -lcurl -ljsoncpp main.cpp
** Usage: ./nomadfs -o direct_io /path/to/mount
**
** Note direct_io is mandatory right now until we can get key size in getattrs.
** Environment Variables: 
	NOMAD_ADDR			nomad addr.  Example: "https://localhost:4646"
	NOMAD_TOKEN			optional nomad token for auth.
	NOMADFS_LOG			optional log file path.
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
#include <set>

#include "StdColors.h"
#include <fuse.h>

using namespace std;

// Global api version is static.
const string apiVers = "/v1";

// Set logs to other options via nomadFS_LOG or default to std::cout
ostream *logs = &cout;

// Keep a set of files (jobs) we've created.  Sadly there's no placeholder or null job.
set<string> createds;

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
// TODO: change stringstream reference to ptr as we don't always need it.
int	nomadCURL(string url, stringstream &httpData, string request = "GET", const string data = "")
{
	int httpCode = 0;
	const char *addr = getenv("NOMAD_ADDR");
	static const string tokenHead = "X-Nomad-Token: ";
	struct curl_slist *headers = NULL;
	CURL* curl;

	if (addr)
		url = (string)addr + url;	// + dc;
	else
		url = (string)"http://localhost:4646" + url;
	
	#if DEBUG
	*logs << CYAN << url << RESET << endl;
	#endif

	// Multi-thread mode is a race condition mine field, so we'll just lock here.
	// Not an issue when single-threaded.  I spent hours on this and this line seems the best fix.
	// Destructor of lk will release this mutex in any case.
	{
		lock_guard<mutex> lk(curlmutex); // DON'T move this -- the race condition gods
		if (!(curl = curl_easy_init()))
			return -1;
		
		// Beware error handling (lack).
		if (getenv("NOMAD_TOKEN"))
			headers = curl_slist_append(headers, (tokenHead + getenv("NOMAD_TOKEN")).c_str());
		
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
int	nomadCURLjson(string url, Json::Value &jsonData, string request = "GET", string post = "")
{
	stringstream stream;
	if (nomadCURL(url, stream, request, post))
		return -EINVAL;

	stream >> jsonData;
	return 0;
}

// We need to assume quite a few attrs.
// Use key trailing slash to identify dir/file.
int nomad_getattr(const char *path, struct stat *stat)
{
	string p(path), key;
	Json::Value keys;
	stringstream sstream;

	stat->st_uid = getuid();
	stat->st_gid = getgid();
	stat->st_blocks = 
	stat->st_blksize = 
	stat->st_size = 0;

	// Be careful with timestamp - file will always appear modified on disk.
	// If using rsync, disable timestamp comparisons.
	stat->st_atime = stat->st_mtime = stat->st_ctime = time(NULL);

	// For now we just support jobs endpoint.
	if (p == "/" || p == "/job")
	{
		stat->st_mode = S_IFDIR | 0700;
		return 0;
	}

	stat->st_mode = S_IFREG | 0600;

	// Is this a blank job we've created locally and are now writing?
	if (createds.find(path) != createds.end())
		return 0;

	// Else check if we're in Nomad already.
	// Chop off ".json" and check if we're 404.  Otherwise we're a file with 600 perms.
	p = p.substr(0, p.length() - 5);
	if (nomadCURL(apiVers + p.substr(), sstream))
		return -ENOENT;

	return 0;
}

// Read ops seem fairly simple, but as we need direct_io and can't guess size, 2 reads are necessary.
// It would be more efficient to read in the entire buffer in OPEN, and free it in RELEASE, but this works.
// Read once, fetch.  Read again to verify 0 bytes left.  This won't scale with latency.
int nomad_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	string data, p(path);
	stringstream sstream;

	// If we're a new file - just read 0.
	if (createds.find(path) != createds.end())
		return 0;
	
	// Chop off pseudo ".json" we added.
	p = p.substr(0, p.length() - 5);
	if (nomadCURL(apiVers + p.substr() + "?pretty", sstream))
		return -ENOENT;

	data = sstream.str();
	if (data.length() - offset <= 0)
		return 0;

	strncpy(buf + offset, data.c_str(), data.length());
	return data.length();
}

// Writes are straightforward.  Should verify size < nomad maximum though the API should do that.
int nomad_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	stringstream stream;
	char *tmp = new char[size + 1];

	// Ugly and shameful but it's const and I'm mixing languages.
	memcpy(tmp, buf, size);
	string jobspec = (string) "{\"Job\":" + tmp + "}";
	delete [] tmp;

	if (nomadCURL(apiVers + "/jobs", stream, "POST", jobspec.c_str()))
		return -EINVAL;
	
	if (createds.find(path) != createds.end())
		createds.erase(path);
	return size;
}

// Return stat of root fs (partition).
int nomad_statfs(const char *path, struct statvfs *statv)
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

// List directory contents.  Currently only /job
int nomad_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	Json::Value jobs;
	string p(path), f;
	set<string> names;

	if (p == "/")
	{
		filler(buf, "job", NULL, 0);
		return 0;
	}

	// Ugly API ambiguity /job /jobs
	if (p == "/job" && nomadCURLjson(apiVers + "/jobs", jobs))
		return -ENOENT;

	for (Json::Value::const_iterator itr = jobs.begin() ; itr != jobs.end() ; itr++ )
	{
		string jobname = (*itr)["ID"].asString();
		filler(buf, (jobname + ".json").c_str(), NULL, 0);
	}

	return 0;
}

// Need to implement this for truncate/write even though we do nothing.
int nomad_truncate(const char *path, off_t newsize)
{
	return 0;
}

// Write a blank key
int nomad_mkdir(const char *path, mode_t mode)
{
	return nomad_write(((string)path + '/').c_str(), "", 0, 0, NULL);
}

// Ignore any problems here but don't dare return failure. 🇺🇸
int nomad_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	// Use a local placeholder.
	// Nomad doesn't have a null/create job as such
	// But if we create a file, we need to not return -ENOENT on write.
	createds.insert(path);
	return 0;
}

// Nomad uses a GC for dead jobs but we can delete with purge.
int nomad_unlink(const char *path)
{
	stringstream stream;
	string p(path);

	// Remove the ".json" exention we added.
	p = p.substr(0, p.length() - 5);
	if (nomadCURL(apiVers + p + "?purge=true", stream, "DELETE"))
		return -EINVAL;
	return 0;
}

int nomad_chmod(const char *path, mode_t mode)
{
	return 0;
}

// Init curl subsystem and set up log stream.
void* nomad_init(struct fuse_conn_info *conn)
{
	curl_global_init(CURL_GLOBAL_ALL);

	// Set nomadFS_LOGS env var to log destination if necessary.
	// Default to cout, which is ignored without -d or -f arg.
	if (getenv("NOMADFS_LOG"))
	{
		if (!(logs = new ofstream(getenv("NOMADFS_LOG"), ofstream::out)))
		{
			cerr << RED << "Unable to open log output file for writing: " << getenv("NOMADFS_LOG") << endl;
			cerr << "Will revert back to std::cout" << RESET << endl;
			logs = &cout;
		}
	}

	// Set dc global if we need to.
	//if (getenv("nomadFS_DC"))
	//	dc = (string)"dc=" + getenv("NOMAD_DC");

	// Always big writes... 4k may not be enough.
	conn->want |= FUSE_CAP_BIG_WRITES;

	return NULL;
}

// Free up curl resources.
void nomad_destroy(void* private_data)
{
	curl_global_cleanup();
}

// Set up function pointers and return a fuse_operations struct.
int main(int argc, char *argv[])
{
	struct fuse_operations fuse = 
	{
		.getattr = nomad_getattr,
		.mkdir = nomad_mkdir,
		.unlink = nomad_unlink,
		.chmod = nomad_chmod,
		.truncate = nomad_truncate,
		.read = nomad_read,
		.write = nomad_write,
		.statfs = nomad_statfs,
		.readdir = nomad_readdir,
		.init = nomad_init,
		.destroy = nomad_destroy,
		.create = nomad_create,
	};

	if ((getuid() == 0) || (geteuid() == 0))
		cerr << YELLOW << "WARNING Running a FUSE filesystem as root opens security holes" << endl;
	
	return fuse_main(argc, argv, &fuse, NULL);
}
