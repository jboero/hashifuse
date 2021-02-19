/****************************************************************************
**
** openapifs - FUSE client for browsing Swagger/OpenAPI endpoints via schema.
**
** Authored by John Boero
** Build instructions: g++ -D_FILE_OFFSET_BITS=64 -lfuse -lcurl -ljsoncpp main.cpp
** Usage: openapifs -s -o direct_io /path/to/mount
**
** Note single threaded mode is required (-s)
** Note direct_io is required (-o direct_io)
** Environment Variables: 
	API_ADDR			Base api address.
		Example: "https://localhost:8200"
	API_SPEC			Full location of OpenAPI v3 spec.
		Example: "https://localhost:8200/v1/openapi"
	API_TOKEN			Full auth bearer token header.
		Example: "Authorization: Bearer d8e8fca2dc0f896fd7cb4cb0031ba249"
	API_CACHE_TTL		Seconds of inactivity before we flush all cache.  Default 300s.
		Example: "300"
****************************************************************************/

#define _GNU_SOURCE 1
#define CURL_STATICLIB
#define FUSE_USE_VERSION 30

#include <string>
#include <sstream>
#include <set>
#include <vector>
#include <iostream>
#include <algorithm>
#include <regex>
#include <mutex>
#include <fstream>
//#include <filesystem>	// C++17 required

#include <string.h>
#include <libgen.h>
#include <curl/curl.h>
#include <json/json.h>

#include <unistd.h>
#include <sys/xattr.h>
#include <stdarg.h>
#include <fuse.h>

// Term colors for stdout
const char RESET[]	= "\033[0m";
const char RED[]	= "\033[1;31m";
const char YELLOW[]	= "\e[0;33m";
const char CYAN[]	= "\e[1;36m";
const char PURPLE[]	= "\e[0;35m";
const char GREEN[]	= "\e[0;32m";
const char BLUE[]	= "\e[0;34m";

using namespace std;

// Ugly globals as we don't get full process control.
// Cache the OpenAPI spec
Json::Value schema;

// Enable log options.
ostream *logs = &cout;

// Protect multi-threaded mode from libcurl/libopenssl race condition.
mutex curlmutex;

// Global cache locally since libCurl doesn't support it.
map<string, string> cache;
time_t cache_timestamp = time(NULL);
int cache_ttl = 300;
string apiaddr, response;

// CURL callback
namespace
{
    std::size_t callback(const char* in, std::size_t size, std::size_t num, char* out)
    {
        string data(in, (size_t) size * num);
        *((stringstream*) out) << data;

        #if DEBUG
		*logs << GREEN << data << RESET << endl;
		#endif
        return size * num;
    }
}

// openapifs GET raw via libcurl
int	apiCURL(string url, stringstream &httpData, string request = "GET", const string post = "")
{
	int res = 0, httpCode = 0;
	struct curl_slist *headers = curl_slist_append(NULL, getenv("API_TOKEN"));
	//headers = curl_slist_append(headers, "Content-Type: application/vnd.api+json");
	static CURL* curl = curl_easy_init();

	//url = (string)getenv("API_ADDR") + url;
	if (time(NULL) - cache_timestamp > cache_ttl)
		cache.clear();
	try
	{
		string val = cache.at(url);
		httpData << val;
		cout << GREEN << "Using cache for " << url << RESET << endl;
		return 0;
	}
	catch (exception &e)
	{
		cerr << "Cache: " << e.what()<<endl;
	}

	#if DEBUG
	*logs << CYAN << url << RESET << endl;
	#endif

	{
		lock_guard<mutex> lk(curlmutex);
		if (!curl)
			return -1;

		if ((res = curl_easy_setopt(curl, CURLOPT_URL, url.c_str())))
			return res;

		if ((res = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.c_str())))
			return res;

		if (request == "POST" && post != "")
		{
			if ((res = curl_easy_setopt(curl, CURLOPT_POST, 1)))
				return res;

			if ((res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str())))
				return res;
	 	}
		
		if ((res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers)))
			return res;

		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &httpData);

		// Optional setting CA bundle... not ideal but libcurl doesn't use env variables.
		if (access("~/openapifs.pem", F_OK) != -1)
			curl_easy_setopt(curl, CURLOPT_CAINFO, "~/openapifs.pem");

		#if DEBUG
		if (post != "")
			*logs << YELLOW << post << RESET << endl;
		#endif 

		curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
		curl_slist_free_all(headers);
		cache[url] = httpData.str();
		cout << GREEN << "Cache size is " << cache.size() << RESET << endl;
	}

	if (httpCode < 200 || httpCode >= 300)
	{
		*logs << RED 
			<< "Couldn't " << request << " " << post << " -> " 
			<< url << " HTTP" << httpCode 
			<< RESET << endl;
		return httpCode;
	}

	cache_timestamp = time(NULL) + cache_ttl;
	return res;
}

int	apiCURLjson(string url, Json::Value &jsonData, string request = "GET", string post = "")
{
	stringstream stream;
	Json::CharReaderBuilder jsonReader;
	int	res = 0;

	if ((res = apiCURL(url, stream, request, post)))
		return res;

	try
	{
		stream >> jsonData;
	}
	catch (exception &e)
	{
		cerr <<YELLOW<< "WARNING JSON problem.  Possibly data is too large for maxread: " << e.what() <<RESET<<endl;
	}

	return 0;
}

int api_getattr(const char *path, struct stat *stat)
{
	const string p(path);
	Json::Value spec = schema["paths"][p];

	stat->st_uid = getuid();
	stat->st_gid = getgid();
	stat->st_blocks = 
	stat->st_blksize = 
	stat->st_size = 0;
	stat->st_atime = stat->st_mtime = stat->st_ctime = time(NULL);
	stat->st_mode = S_IFDIR | 0700;			// Dir default

	if (p == "/clear_cache")
		stat->st_mode = S_IFREG | 0200;		// Any write /clear_cache
	else if (p == "/response")
		stat->st_mode = S_IFREG | 0400;		// Read last response
	else if (regex_match(p, (regex)"^(.*).json$"))
		stat->st_mode = S_IFLNK | 0777;		// .json symlinks
	else if (regex_match(p, (regex)"^(.*)\\{(.*)\\}$"))
		stat->st_mode = S_IFREG | 0000;		// If using {name} examples
	else if (regex_match(p, (regex)"^(.*)/(post|put|delete|patch|.*\\..*)$"))
		stat->st_mode = S_IFREG | 0600;
	else if (regex_match(p, (regex)"^(.*)/(get|description|summary|options|trace|servers|parameters)$"))
		stat->st_mode = S_IFREG | 0400;
	
	return 0;
}

int api_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	// To prevent double reads, static buffer last read (single thread only!)
	string buffer, p(path), token, bname, dname;
	int len = size;
	Json::Value jbuf;

	if (regex_match(p, (regex)".*.json"))
	{
		((char*)path)[p.length() - 5] = '\0';
		p = path;
	}

	if (p == "/response")
	{
		strncpy(buf + offset, response.c_str() + offset, len);
		return len;
	}

	bname = basename((char*)path);
	dname = dirname((char*)path);
	try
	{
		jbuf = schema["paths"][dname];
		// If post, fetch template with schema.
		if (bname == "post")
			buffer = (string)"{\n\t\"$schema\":\"" + getenv("FUSEPATH") + p + ".schema\"\n}\n";
		else if (bname == "post.schema")	// Schema has a slash (application/json) so shortcut it.
			buffer = jbuf["post"]["requestBody"]["content"]["application/json"]["schema"].toStyledString();
		// If get or get?params=etc, but not get.sub
		else if (bname == "description")
			buffer = jbuf["description"].asString();
		else if (regex_match(p, (regex)"^(.*)/get([^.]|$)"))
		{
			stringstream res;
			if (apiCURL(apiaddr + dname, res))
				return -ENOENT;
			
			buffer = res.str();
		}
		else // Interpret dots ala jq to select json (even without readdir)
		{
			stringstream dots(bname);
			while (getline(dots, token, '.'))
				jbuf = jbuf[token];
		
			buffer = jbuf.toStyledString();
		}

		len = min((ulong)size, (ulong)(buffer.length() - offset));

		strncpy(buf + offset, buffer.c_str() + offset, len);
	}
	catch (exception &e)
	{
		cerr <<YELLOW<< "WARNING: " << e.what() <<RESET<<endl;
		return -ENOENT;
	}
	return len;
}

int api_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	string p(path), dname, verb;
	stringstream stream;

	verb = basename((char*)path);
	dname = dirname((char*)path);

	// TODO patch vars...
	if (apiCURL(apiaddr + p, stream, verb, buf))
		return -EINVAL;
	else
		response = stream.str();
	
	return size;
}

// Return stat of root fs (partition).
int api_statfs(const char *path, struct statvfs *statv)
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

// Helper function to readdir list json array
void fillArray(Json::Value &array, void *buf, fuse_fill_dir_t filler)
{
	for (Json::Value::ArrayIndex i = 0; i != array.size(); ++i)
	{
		string key = array[i]["id"].asString();
		size_t slash = key.find('/');
		if (slash != string::npos)
			key = key.substr(0, slash);

		filler(buf, key.c_str(), NULL, 0);
	}
}

// Readdir manually builds out dir structure based on /sys/mounts
// Note that as we're DIRECT_IO and don't necessarily know what's out there,
// we can't use READDIR_PLUS sadly.  I started to implement this in FUSE3 but had issues.
int api_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	string p(path);
	set<string> uniques;
	smatch match;
	Json::Value paths = schema["paths"];
	Json::Value desc  = paths[p];
	Json::Value::Members rootContents = paths.getMemberNames();

	if (p == "/")
	{
		uniques.insert("clear_cache");
		uniques.insert("response");
		uniques.insert("response.json");
	}
	else
	{
		// Get spec attribs
		Json::Value::Members ops = desc.getMemberNames();
		for(vector<string>::const_iterator i = ops.begin(); i!= ops.end(); i++)
		{
			if (regex_match(*i, (regex)"^.*\\.\\*$"))
				continue;
			
			uniques.insert(*i);
			uniques.insert(*i + ".json");
		}
	}

	cout <<GREEN<< p <<RESET<<endl;

	regex r("(" + p.substr(1) + "/)[^/]+");

	// Get obvious members.
	for(vector<string>::const_iterator i = rootContents.begin(); i!= rootContents.end(); i++)
	{
		if (regex_match(*i, (regex)"^.*\\.\\*$"))
			continue;
		
		if (regex_search(i->begin(), i->end(), match, r))
		{
			string m = match.str(0);
			uniques.insert(m.substr(p.length()));
		}
	}

	// HashiCorp uses {name} type placeholders instead of list attrib
	if (p[p.length() - 1] == '}')
	{
		Json::Value list;
		string basepath = basename((char*)p.c_str());
		apiCURLjson(apiaddr + basepath, list, "LIST");
		for (Json::Value::ArrayIndex i = 0; i != list["data"]["keys"].size(); ++i)
		{
			string key = list["data"]["keys"][i].asString();
			size_t slash = key.find('/');
			if (slash != string::npos)
				key = key.substr(0, slash);

			filler(buf, key.c_str(), NULL, 0);
		}
	}

	for (set<string>::iterator it = uniques.begin(); it != uniques.end(); ++it)
		filler(buf, it->c_str(), NULL, 0);

	return 0;
}

// Implicit link from ${VALUE}.json to ${VALUE}
static int api_readlink(const char *path, char *buf, size_t size)
{
	char *bname = basename((char*)path);
	int blen = strlen(bname);

	cout <<BLUE<< bname <<RESET<< endl;
	if (regex_match(path, (regex)"^(.*).json$"))
		strncpy(buf, bname, blen - 5);
	else
		return 1;
	
	return 0;
}

void* api_init(struct fuse_conn_info *conn)
{
	curl_global_init(CURL_GLOBAL_ALL);
	conn->want |= FUSE_CAP_BIG_WRITES;

	// Did we specify cache expiration seconds?
	if (getenv("API_CACHE_TTL"))
		cache_ttl = atoi(getenv("API_CACHE_TTL"));
	if (getenv("API_ADDR"))
		apiaddr = getenv("API_ADDR");
	
	apiCURLjson(getenv("API_SPEC"), schema);

	return NULL;
}

// Stub required for truncate/write.
int api_truncate(const char *path, off_t newsize)
{
	return 0;
}

// RFE: Could add api metadata and mount types as xattrs.
// RFE: Sanitize inputs and env variables.
int main(int argc, char *argv[])
{
	struct fuse_operations fuse = 
	{
		.getattr = api_getattr,
		.readlink = api_readlink,
		.truncate = api_truncate,
		.read = api_read,
		.write = api_write,
		.statfs = api_statfs,
		.readdir = api_readdir,
		.init = api_init,
	};

	if ((getuid() == 0) || (geteuid() == 0))
		cerr << YELLOW << "WARNING Running a FUSE filesystem as root opens security holes" << endl;
	
	// Save last arg mount path in env var
	if (argc > 1)
		setenv("FUSEPATH", argv[argc-1], 0);
	else
		return 1;
	
	return fuse_main(argc, argv, &fuse, NULL);
}
