﻿# VaultFS
Simple browseable CRUD dir+secret structure of Vault KV secrets.  It's written in C++ with FUSE v28, libCurl, libJSONcpp.  Support exists for KV, SSH, TOTP, PKI, sys, Cubbyhole, and may work for others by default.

Demo: https://youtu.be/S_3j9Awlu-o

# Running VaultFS
Assuming a local vault instance is running, use environment variables just like Vault CLI:

```
$ export VAULT_ADDR=http://localhost:8200
$ export VAULT_TOKEN=[YOUR TOKEN]
$ vaultfs -o direct_io /mnt/vault (or your mount path)
```
In the event you need to specify a CA bundle, libcurl doesn't seem to use curl's standard environment variables.  Instead you can place your PEM bundle into ~/vaultfs.pem and vaultfs will attempt to use it.  This allows self-signed certs which isn't recommended for production.

For debugging, best results via single threaded DEBUG build:
```
$ vaultfs -d -s -o direct_io /mnt/vault
```

# Stopping VaultFS
To stop vaultfs running, or reset it in event of an issue, just dismount the fs:
```
fusermount -u /mnt/vault (or your mount path)
```

# Browsing Vault
Vault secrets can be browsed like a local filesystem.  

```
$ tree /mnt/vault
/mnt/vault
├── cubbyhole
│   └── test
├── database
├── identity
├── kv
│   ├── test
│   └── test2
├── kv2
│   ├── testdir
│   └── testkv2
├── pki
│   ├── ca
│   │   └── pem
│   ├── certs
│   │   ├── 47-6c-6e-6d-67-5c-a6-b9-14-3d-70-f9-c7-cf-23-f6-55-e1-60-9b
│   │   ├── 56-dd-a3-61-f9-22-df-4f-be-cf-d4-f6-98-e9-59-51-9c-8b-45-44
│   │   ├── ca
│   │   ├── ca_chain
│   │   └── crl
│   ├── creds
│   └── roles
│       ├── johnnyb.mawenzy.com
│       └── test.johnnyb.mawenzy.com
├── secret
│   └── test
├── ssh
│   ├── ca
│   │   └── pem
│   ├── certs
│   ├── creds
│   └── roles
│       └── test
├── sys
│   ├── config
│   ├── health
│   ├── license
│   ├── mounts
│   ├── namespaces
│   ├── replication
│   └── tools
├── totp
│   ├── keys
│   │   └── my-key
│   └── random
└── transit
    ├── keys
    │   ├── encrypt
    │   └── signer
    └── random

23 directories, 26 files
$ 
```

Secrets can be read but KV secrets only one level deep are currently supported.  Dir/secret ambiguity in KV makes it impossible for getattr to determine what's a dir and what's a secret.
```
$ cat /mnt/vault/kv/test
{
        "sec" : "addedfromGUI",
        "sec3" : "updated from fs",
        "sec5" : "added from fs"
}
$ cat /mnt/vault/pki/ca/pem
-----BEGIN CERTIFICATE-----
MIIDNTCCAh2gAwIBAgIUVt2jYfki30++z9T2mOlZUZyLRUQwDQYJKoZIhvcNAQEL
BQAwFjEUMBIGA1UEAxMLbWF3ZW56eS5jb20wHhcNMTkwMjExMTEyMjMxWhcNMTkw
[TRUNCATED]
A+SqeEKodL5zZI6C7/bl/6AFXd3qXUMxy6RE8A3D9dEBi0cQ1Pekt0I/FTTUY+FG
+WyEvaNNkQG3
-----END CERTIFICATE-----
```

# Writing and POST Responses
I've added a bit of a hack where writes dump any POST response to your process STDOUT.  This isn't typical behaviour for FUSE (or for any filesystem) but yields really interesting results.  For example if you write to an API post endpoint like "transit encrypt" the HTTP response would usually just be output in the FUSE process which isnt' very helpful.  Now it will dump responses out to your client STDOUT, so you can use `tee` or `cp` on the terminal and you will get valuable output.  The example below copies a json payload to a transit encrypt endpoint.  Suddenly the ciphertext response is dumped to the terminal.
![vaultfstransit](https://user-images.githubusercontent.com/7536012/122565166-1099d980-d03e-11eb-820b-ba5b48698214.gif)
