/****************************************************************************
**
** VaultFS - FUSE client for Hashicorp consul secrets.
**
** Authored by John Boero
** Build instructions: g++ -D_FILE_OFFSET_BITS=64 -lfuse -lcurl -ljsoncpp main.cpp
** Usage: ./vaultfs -o direct_io /path/to/mount
**
** Note direct_io is mandatory right now until we can get key size in getattrs.
** Environment Variables: 
	VAULT_ADDR		vault addr.  Example: "http://localhost:8200"
	VAULT_TOKEN		auth token.
	VAULT_NAMESPACE	optional namespace (enterprise only).

** This code is kept fairly simple/ugly without object oriented best practices.
** TODO: securely destroy strings - https://stackoverflow.com/questions/5698002/how-does-one-securely-clear-stdstring
** TODO: offer ~/.vault-token in addition to env VAULT_TOKEN
****************************************************************************/

#define CURL_STATICLIB
#define FUSE_USE_VERSION 26

#include <string>
#include <string.h>
#include <sstream>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>
#include <regex>

#include <curl/curl.h>
#include <json/json.h>

#include <mutex>
#include <fstream>
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

// Global api version.
const string apiVers = "/v1";

// Set logs to other options (ofstream) or default to std::cout
ostream *logs = &cout;

// Keep/cache a local copy of mount->mount_type for speed.
static Json::Value gMounts;

// Protect multi-threaded mode from libcurl/libopenssl race condition.
mutex curlmutex;

// CURL callback
namespace
{
    std::size_t callback(
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

// Vault GET raw via libcurl
// Currently supports request GET (default), POST, LIST.
// TODO: escape environment variables for injection vulnerabilities.
int	vaultCURL(string url, stringstream &httpData, string request = "GET", const string post = "")
{
	int res = 0, httpCode = 0;
	string tokenHeader = "X-Vault-Token: ";
	string nsHeader = "X-Vault-Namespace: ";
	struct curl_slist *headers = curl_slist_append(NULL, (tokenHeader + getenv("VAULT_TOKEN")).c_str());
	CURL* curl;

	if (getenv("VAULT_NAMESPACE"))
		headers = curl_slist_append(headers, (nsHeader + getenv("VAULT_NAMESPACE")).c_str());

	url = (string)getenv("VAULT_ADDR") + url;

	#if DEBUG
	*logs << CYAN << url << RESET << endl;
	#endif

	{
		lock_guard<mutex> lk(curlmutex);
	
		if (!(curl = curl_easy_init()))
			return -1;
		
		if (res = curl_easy_setopt(curl, CURLOPT_URL, url.c_str()))
			return res;

		if (res = curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.c_str()))
			return res;

		if (request == "POST" && post != "")
		{
			if (res = curl_easy_setopt(curl, CURLOPT_POST, 1))
				return res;

			if (res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str()))
				return res;
	 	}
		
		if (res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers))
			return res;

		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &httpData);

		// Optional setting CA bundle... not ideal but libcurl doesn't use env variables.
		if (access("~/vaultfs.pem", F_OK) != -1)
			curl_easy_setopt(curl, CURLOPT_CAINFO, "~/vaultfs.pem");

		#if DEBUG
		if (post != "")
			*logs << YELLOW << post << RESET << endl;
		#endif 

		curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
		curl_easy_cleanup(curl);
		curl_slist_free_all(headers);
	}

	if (httpCode < 200 || httpCode >= 300)
	{
		*logs << "Couldn't " << request << " " << post << " -> " << url << " HTTP" << httpCode << endl;
		return httpCode;
	}

	return res;
}

int	vaultCURLjson(string url, Json::Value &jsonData, string request = "GET", string post = "")
{
	stringstream stream;
	Json::CharReaderBuilder jsonReader;
	int	res = 0;

	if (res = vaultCURL(url, stream, request, post))
		return res;

	stream >> jsonData;

	return 0;
}

// Cache /sys/mounts for speed.
int cacheMounts()
{
	Json::Value mounts;
	vector<string> removers;
	if (vaultCURLjson(apiVers + "/sys/mounts", gMounts))
		return -EINVAL;
	
	for (Json::Value::const_iterator it = gMounts.begin(); it != gMounts.end(); ++it)
		if (!(it->isObject() && it->isMember("type")))
			removers.push_back(it.key().asString());
	
	// Clear out garbage members that aren't actually mounts with type.
	for (vector<string>::iterator rm = removers.begin(); rm != removers.end(); ++rm)
		gMounts.removeMember(*rm);
	
	return 0;
}

string getMountType(string path)
{
	size_t pos;

	if (path[0] == '/')
		path = path.substr(1);

	if ((pos = path.find('/')) != string::npos)
		path = path.substr(0, pos + 1);

	if (gMounts.isMember(path))
		return gMounts[path]["type"].asString();

	return "";
}

int vault_getattr(const char *path, struct stat *stat)
{
	const string p(path);
	const size_t slashes = count(p.begin(), p.end(), '/');
	int res = 0;
	Json::Value json, dir;

	stat->st_uid = getuid();
	stat->st_gid = getgid();
	stat->st_blocks = 
	stat->st_blksize = 
	stat->st_size = 0;

	// Be careful with timestamp - file will always appear modified on disk.
	stat->st_atime = stat->st_mtime = stat->st_ctime = time(NULL);
	//stat->st_atime = stat->st_mtime = stat->st_ctime = 0;

	// Due to crude and ambiguous attrs we can't determine secret or dir
	// In fact, you can have both /path/secret and /path/secret/ which is ugly.
	// FUSE automatically truncates trailing / during traversal.
	// The only way to function is assume one layer deep (1 slash) is a dir, else file.
	if (slashes <= 1)
	{
		stat->st_mode = S_IFDIR | 0700;	// Directory plus execute perm
		return 0;
	}

	string mountType = getMountType(p);
	if (mountType == "")
		return -ENOENT;

	// Get capabilities for this path.
	// example payload: {"paths": ["secret/foo"]}
	//	const string post = (string)"{\"paths\": [\"" + path + "\"]}";
	//	if (res = vaultCURLjson(apiVers + "/sys/capabilities-self", json, "POST", post))
	//		return -EINVAL;
	if (mountType == "kv")
	{
		if (slashes > 1)
			stat->st_mode = S_IFREG | 0600;
		else
			stat->st_mode = S_IFDIR | 0700;
	}
	else if (regex_match(mountType, (regex)"(pki|ssh|transit|totp|aws|gcp|azure)"))
	{
		if (slashes > 2)
			stat->st_mode = S_IFREG | 0600;
		else
			stat->st_mode = S_IFDIR | 0700;
 
		return 0;
	}
	else if (mountType == "system")
	{
		if (regex_match(p, (regex)"(.*)/(leases|tools|namespaces|config|policy)"))
			stat->st_mode = S_IFDIR | 0700;
		else
			stat->st_mode = S_IFREG | 0600;
	}
	else
	{
		stat->st_mode = S_IFREG | 0600;
	}

	// Isolate the single dir/mount we need.
	//TODO - this is phantom creation....
	//dir = gMounts[p];
	/*
	for (Json::Value::ArrayIndex i = 0; i != dir.size(); ++i)
	{
		if (!dir[i].isString())
			continue;
		
		string cap = dir[i].asString();

		if (cap == "root" || cap == "sudo")
			stat->st_mode |= 0600;
		else if (cap == "read")
			stat->st_mode |= 0400;
		else if (cap == "write")
			stat->st_mode |= 0200;
		else if (cap == "list")
			stat->st_mode |= S_IFDIR | 0100;
	}
	*/
	return 0;
}

int vault_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	static string raw;
	string p(path + 1), mountType;
	Json::Value mount, data;
	Json::StreamWriterBuilder builder;
	stringstream stream;
	int res, len;
	size_t mlen;

	if (offset > 0)
		return 0;
	
	// Allow manual refresh of mounts cache via reading /sys/mounts :)
	if (p == "/sys/mounts")
		cacheMounts();

	// Need to get mount type to figure out how to read this path.
	if (res = vaultCURLjson(apiVers + "/sys/mounts", mount))
		return -EINVAL;

	if ((mlen = p.find('/')) == string::npos)
		return -ENOENT;
	
	// Need to get the first level of path for mount details.
	string mpath = p.substr(0, mlen + 1);
	if (mount.isMember(mpath))
			mount = mount[mpath];
		else
			return -ENOENT;

	mountType = mount["type"].asString();

	// Different ways to read kv versions.
	if (mountType == "kv")
	{
		if (!mount["options"].isNull() && mount["options"]["version"].isString())
		{
			if (mount["options"]["version"].asString() == "2")
				p.insert(mpath.length(), "data/");
		}
	}

	/*********************************************************************/
	// Rewrite options:
	if (mountType == "pki")
	{
		if (regex_match(p, (regex)"^(.*)/certs/(.*)$"))
		{
			// Annoyingly, list is /pki/certs, but read is /pki/cert/$serial
			// We need to pick out that pesky 's'
			size_t s = p.find("/certs/");
			p.erase(s + 5, 1);
		}
		else if (regex_match(p, (regex)"^(.*)/ca/pem$"))
		{
			if (res = vaultCURL(apiVers + '/' + p, stream))
				return -ENOENT;
			raw = stream.str();
		}
	}
	else
	{
		if (res = vaultCURLjson(apiVers + '/' + p, data))
			return -ENOENT;
		
		// Because some secret engines have ".data.data"...
		// Beware someone actually calling a secret "data"
		while (data.isObject() && data.isMember("data"))
			data = data["data"];

		raw = Json::writeString(builder, data);
	}

	len = min(size, raw.length());

	// We've reached the end of the file? (DIRECT_IO)
	// Unfortunately this usually means double reads :/
	if (offset >= len)
		return 0;
	
	strncpy(buf + offset, raw.c_str(), len);
	return len;
}

int vault_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	string p(path + 1), payload(buf);
	Json::Value mount, data;
	Json::StreamWriterBuilder builder;
	stringstream stream;
	size_t mlen;

	// Need to get mount type to figure out how to read this path.
	//if (res = vaultCURLjson("/v1/sys/mounts", mount))
	//	return -EINVAL;

	if ((mlen = p.find('/')) == string::npos)
		return -ENOTDIR;

	// KV2 Need to re-wrap ourselves in data:{}
	//payload = "{\"data\":" + payload + "}";
	//p.insert(mlen, "/data");

	if (vaultCURL(apiVers + '/' + p, stream, "POST", payload.c_str()))
		return -EINVAL;

	return size;
}

// Return stat of root fs (partition).
int vault_statfs(const char *path, struct statvfs *statv)
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
		string key = array[i].asString();
		size_t slash = key.find('/');
		if (slash != string::npos)
			key = key.substr(0, slash);

		filler(buf, key.c_str(), NULL, 0);
	}
}

// Helper function to readdir list json members
void fillMembers(Json::Value &root, void *buf, fuse_fill_dir_t filler)
{
	vector<string> keys = root.getMemberNames();
	for (vector<string>::iterator iter = keys.begin(); iter != keys.end(); ++iter)
	{
		string::size_type pos = iter->find("/");
		if (pos != string::npos)
		    iter->erase(pos);
		filler(buf, iter->c_str(), NULL, 0);
	}
}

// Readdir manually builds out dir structure based on /sys/mounts
// Note that as we're DIRECT_IO and don't necessarily know what's out there,
// we can't use READDIR_PLUS sadly.  I started to implement this in FUSE3 but had issues.
int vault_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	Json::Value mount, keys;
	string p(path), smount, mountType;
	int res = 0;
	
	// Root?
	if (p == "/")
	{	
		vector<string> members = gMounts.getMemberNames();
		for(vector<string>::iterator iter = members.begin(); iter != members.end(); ++iter)
		{
			// Remove trailing /
			if (iter->find('/') == iter->length() - 1)
				iter->pop_back();
			filler(buf, iter->c_str(), NULL, 0);
		}
		return 0;
	}

	p = p.substr(1) + '/';
	smount = p.substr(0, p.find('/')) + '/';

	// Isolate the single mount we need.
	if (gMounts.isMember(smount))
		mount = gMounts[smount];
	else
		return -ENOENT;
	
	mountType = mount["type"].asString();

	// KV version 1 is indicated by options=NULL 
	// whereas version 2, options="version=2"
	if (mountType == "kv")
	{
		if (!mount["options"].isNull() && mount["options"]["version"].isString())
		{
			// Really just keep going and try to read using v2 style...
			// May need to add v2, v3, etc. later
			string vers = mount["options"]["version"].asString();
			p = p.substr(0, p.length() - 1);
			string secdir = p.substr(smount.length() - 1);

			if (vers == "2")
			{
				if (res = vaultCURLjson(apiVers + '/' + smount + "metadata/" + secdir, keys, "LIST"))
					return -ENOENT;
			}
		}
	}
	else if (regex_match(mountType, (regex)"(pki|ssh)"))
	{
		if (p == smount)	// If we're just listing at the /pki level,
		{
			filler(buf, "ca", NULL, 0);
			filler(buf, "roles", NULL, 0);
			filler(buf, "certs", NULL, 0);	// Only for PKI
			filler(buf, "creds", NULL, 0);	// Only for SSH
		}
		else if (p == smount + "certs/")
		{
			filler(buf, "ca", NULL, 0);
			filler(buf, "crl", NULL, 0);
			filler(buf, "ca_chain", NULL, 0);
			if (res = vaultCURLjson(apiVers + '/' + smount + "/certs", keys, "LIST"))
				return -ENOENT;
			fillArray(keys["data"]["keys"], buf, filler);
		}
		else if (p == smount + "roles/")
		{
			if (res = vaultCURLjson(apiVers + '/' + smount + "/roles", keys, "LIST"))
				return -ENOENT;
			fillArray(keys["data"]["keys"], buf, filler);
		}
		else if (p == smount + "ca/")
			filler(buf, "pem", NULL, 0);
		
		return 0;
	}
	else if (regex_match(mountType, (regex)"(transit|totp)"))
	{
		if (p == smount)
		{
			filler(buf, "keys", NULL, 0);
			filler(buf, "random", NULL, 0);
			return 0;
		}
		//else if (p == smount + "keys/") // fallback to default handler.
		// TODO browse versions.
	}
	else if (mountType == "gcp")
	{
		if (p == smount)
		{
			filler(buf, "config", NULL, 0);
			filler(buf, "roleset", NULL, 0);
			filler(buf, "token", NULL, 0);
			filler(buf, "key", NULL, 0);
			return 0;
		}
	}
	else if (mountType == "aws" || mountType == "azure")
	{
		if (p == smount)
		{
			filler(buf, "config", NULL, 0);
			filler(buf, "roles", NULL, 0);
			filler(buf, "creds", NULL, 0);
			filler(buf, "sts", NULL, 0);
			return 0;
		}
	}
	else if (mountType == "system")
	{
		if (p == "sys/")
		{
			filler(buf, "config", NULL, 0);
			filler(buf, "health", NULL, 0);
			filler(buf, "license", NULL, 0);
			filler(buf, "namespaces", NULL, 0);
			filler(buf, "mounts", NULL, 0);
			filler(buf, "replication", NULL, 0);
			filler(buf, "tools", NULL, 0);
			filler(buf, "policy", NULL, 0);
			return 0;
		}
		else if (p == "sys/policy/")
		{
			if (res = vaultCURLjson(apiVers + "/sys/policies", keys, "LIST"))
				return -ENOENT;
		}
	}

	// TODO add other types or inheritance
	// Generic LIST of secrets if we didn't handle it:
	if (keys == Json::nullValue)
		if (res = vaultCURLjson(apiVers + '/' + p, keys, "LIST"))
			return -ENOENT;
	
	fillArray(keys["data"]["keys"], buf, filler);
	return 0;
}

void* vault_init(struct fuse_conn_info *conn)
{
	curl_global_init(CURL_GLOBAL_ALL);
	conn->want |= FUSE_CAP_BIG_WRITES;

	cacheMounts();

	return NULL;
}

// Need to implement this for truncate/write.
int vault_truncate(const char *path, off_t newsize)
{
	return 0;
}

// TODO: Could add vault metadata and mount types as xattrs.

int main(int argc, char *argv[])
{
	struct fuse_operations fuse = 
	{
		.getattr = vault_getattr,
		.truncate = vault_truncate,
		.read = vault_read,
		.write = vault_write,
		.statfs = vault_statfs,
		.readdir = vault_readdir,
		.init = vault_init,
	};

	if ((getuid() == 0) || (geteuid() == 0))
		cerr << YELLOW << "WARNING Running a FUSE filesystem as root opens security holes" << endl;
	
	return fuse_main(argc, argv, &fuse, NULL);
}
