# TFEFS
Simple browseable CRUD dir+workspace structure of Terraform.  It's written in C++ with FUSE v28, libCurl, libJSONcpp.  Support exists for Runs, Plans, Logs, and Policies, and may work for others by default.

Demo: [pending]

# Running TFEFS
Assuming you have a TFE account (SaaS or private on-prem), simply set TFE_ADDR (or leave blank for SaaS) and TFE_TOKEN, just like you would for API calls.

```
$ export TFE_ADDR=http://localhost:8200
$ export TFE_TOKEN=[YOUR TOKEN]
$ ./tfefs -s -o direct_io /mnt/tfe (or your mount path)
```

In the event you need to specify a CA bundle, libcurl doesn't seem to use curl's standard environment variables.  Instead you can place your PEM bundle into ~/TFEFS.pem and TFEFS will attempt to use it.  This allows self-signed certs which isn't recommended for production.

For debugging, best results via single threaded DEBUG build:
```
$ ./tfefs -d -s -o direct_io /mnt/tfe
```

# Stopping TFEFS
To stop TFEFS running, or reset it in event of an issue, just dismount the fs:
```
fusermount -u /mnt/tfe (or your mount path)
```

# Browsing Terraform
Terraform accounts can be browsed like a local filesystem.  
```
[jboero@z600 ~]$ ls /mnt/tfe/organizations/
Claranet-DemoV2  emea-se-playground-2019  hc-solutions-engineering  JoeStack  JohnBoero
[jboero@z600 ~]$ ls /mnt/tfe/organizations/JohnBoero/
policies  policy-sets  ssh-keys  workspaces
[jboero@z600 ~]$ ls /mnt/tfe/organizations/JohnBoero/workspaces
hashihang-eks  hashihang-pods  test3  test-customer  tfe-test
[jboero@z600 ~]$ tree /mnt/tfe/organizations/JohnBoero/workspaces/hashihang-eks
/mnt/tfe/organizations/JohnBoero/workspaces/hashihang-eks
├── applies
├── plans
├── runs
│   ├── run-87inQrQydqej5ZwP
│   ├── run-e1E2XT7RfvWdQvCb
│   ├── run-e28wXYNh8uXdZcb9
│   ├── run-gR6AsNb7jvQDXkdN
│   ├── run-gTwTD3558FoSkCUN
│   ├── run-Je4GJmWupjVbWMJC
│   ├── run-JXz3dKdxgwbpaZH6
│   ├── run-Mf37n6JZ2SwEkbPV
│   ├── run-mP3XSaBVPXVvMb2A
│   ├── run-nScY87UhFUYyCbMm
│   ├── run-PJwmCaJk3kj1HULV
│   ├── run-pvq5inxyA8id9PA3
│   ├── run-QKYJrDdG3ffVHkZG
│   ├── run-QUrW5mh6jcPHNBYA
│   ├── run-rZdDpurGxTJXMN1X
│   ├── run-U7SqwhAiQFznyuwn
│   ├── run-Upyq8S3nphUquSyG
│   ├── run-YdNHPCEhRCJKEHCR
│   ├── run-Yu9r4i67eUDNKXrU
│   └── run-yuLkn5HwfA3SnUyu
├── state-versions
│   ├── sv-aKjvcPnBQ5dgwqPw
│   ├── sv-eYzWkT3XYYNAmYfx
│   ├── sv-f6ji6wXwkV6VZDPo
│   ├── sv-FmoDEf8DCPydgSpa
│   ├── sv-H1icqr14pbep2HmE
│   ├── sv-HFgeFNUVqvn2bxgZ
│   ├── sv-iBz45rVLnH2uHaLa
│   ├── sv-JNnqEbHSuzuqsp8e
│   ├── sv-K3KQnMic9ZjFFM43
│   ├── sv-KuseFMWz4EntG85x
│   ├── sv-kYt9qquSZKSF7bnv
│   ├── sv-Nx3Xpn634sGBwTcj
│   ├── sv-PMseTeRWCbo6ZEKY
│   ├── sv-PtkDpuiaxsPyfdMW
│   ├── sv-r38WYMKMwsczBa7D
│   ├── sv-t79ESwoS2YJde6B3
│   ├── sv-VmPxdjZYbbiyDd6V
│   ├── sv-wNQYubRwZqWUBySj
│   ├── sv-Yen5CpHjvaAbc6UE
│   └── sv-Ytv6PUkT7VxohUjK
└── vars

4 directories, 41 files
[jboero@z600 ~]$ cd /mnt/tfe/organizations/JohnBoero/workspaces/hashihang-eks
[jboero@z600 hashihang-eks]$ jq . runs/run-87inQrQydqej5ZwP
{
  "data": {
    "id": "run-87inQrQydqej5ZwP",
    "type": "runs",
    "attributes": {
      "is-destroy": false,
      "message": "Custom message",
      "source": "tfe-api",
      "status": "errored",
      "status-timestamps": {
        "errored-at": "2018-09-18T12:30:14+00:00",
        "planning-at": "2018-09-18T12:30:06+00:00"
      },
      "created-at": "2018-09-18T12:30:06.489Z",
      "canceled-at": null,
      "has-changes": false,
      "actions": {
        "is-cancelable": false,
        "is-confirmable": false,
        "is-discardable": false,
        "is-force-cancelable": false
      },
      "plan-only": false,
      "permissions": {
        "can-apply": true,
        "can-cancel": true,
        "can-discard": true,
        "can-force-execute": true,
        "can-force-cancel": true
      }
    },
    "relationships": {
      "workspace": {
        "data": {
          "id": "ws-cejaVXJhZHZQsKiR",
          "type": "workspaces"
        }
      },
      "apply": {
        "data": {
          "id": "apply-5fboahZz8ZWGJuC5",
          "type": "applies"
        },
        "links": {
          "related": "/api/v2/runs/run-87inQrQydqej5ZwP/apply"
        }
      },
      "configuration-version": {
        "data": {
          "id": "cv-ZQrPMXFK4eF9bvdU",
          "type": "configuration-versions"
        },
        "links": {
          "related": "/api/v2/runs/run-87inQrQydqej5ZwP/configuration-version"
        }
      },
      "created-by": {
        "data": {
          "id": "user-7FXeTZEX1ofWhJA3",
          "type": "users"
        },
        "links": {
          "related": "/api/v2/runs/run-87inQrQydqej5ZwP/created-by"
        }
      },
      "plan": {
        "data": {
          "id": "plan-wK4czVYiNUHWBQNY",
          "type": "plans"
        },
        "links": {
          "related": "/api/v2/runs/run-87inQrQydqej5ZwP/plan"
        }
      },
      "run-events": {
        "data": [
          {
            "id": "re-u8YkxZdKQHCQXV75",
            "type": "run-events"
          },
          {
            "id": "re-1uidyxacUDb4A8Vz",
            "type": "run-events"
          },
          {
            "id": "re-Mw4ysUM1DK9kprT2",
            "type": "run-events"
          },
          {
            "id": "re-pRDSJGLD18TApaDt",
            "type": "run-events"
          }
        ],
        "links": {
          "related": "/api/v2/runs/run-87inQrQydqej5ZwP/run-events"
        }
      },
      "policy-checks": {
        "data": [
          {
            "id": "polchk-bmRwHL4aKdyiK7PZ",
            "type": "policy-checks"
          }
        ],
        "links": {
          "related": "/api/v2/runs/run-87inQrQydqej5ZwP/policy-checks"
        }
      },
      "comments": {
        "data": [],
        "links": {
          "related": "/api/v2/runs/run-87inQrQydqej5ZwP/comments"
        }
      }
    },
    "links": {
      "self": "/api/v2/runs/run-87inQrQydqej5ZwP"
    }
  }
}
[jboero@z600 hashihang-eks]$ cat vars 
{"data":[{"id":"var-QjAhj6U6hqnTZWVx","type":"vars","attributes":{"key":"AWS_ACCESS_KEY_ID","value":null,"sensitive":true,"category":"env","hcl":false,"created-at":"2018-06-21T17:26:22.570Z"},"relationships":{"configurable":{"data":{"id":"ws-cejaVXJhZHZQsKiR","type":"workspaces"},"links":{"related":"/api/v2/organizations/JohnBoero/workspaces/hashihang-eks"}}},"links":{"self":"/api/v2/vars/var-QjAhj6U6hqnTZWVx"}},{"id":"var-87P1xEnPcte82TRC","type":"vars","attributes":{"key":"AWS_SECRET_ACCESS_KEY","value":null,"sensitive":true,"category":"env","hcl":false,"created-at":"2018-06-21T17:26:22.566Z"},"relationships":{"configurable":{"data":{"id":"ws-cejaVXJhZHZQsKiR","type":"workspaces"},"links":{"related":"/api/v2/organizations/JohnBoero/workspaces/hashihang-eks"}}},"links":{"self":"/api/v2/vars/var-87P1xEnPcte82TRC"}},{"id":"var-Tx4urno2KBFt5ywR","type":"vars","attributes":{"key":"CONFIRM_DESTROY","value":"1","sensitive":false,"category":"env","hcl":false,"created-at":"2018-06-22T10:25:51.346Z"},"relationships":{"configurable":{"data":{"id":"ws-cejaVXJhZHZQsKiR","type":"workspaces"},"links":{"related":"/api/v2/organizations/JohnBoero/workspaces/hashihang-eks"}}},"links":{"self":"/api/v2/vars/var-Tx4urno2KBFt5ywR"}},{"id":"var-QsqT9gg4wMBYf1D6","type":"vars","attributes":{"key":"k8s_cluster_size","value":null,"sensitive":true,"category":"terraform","hcl":false,"created-at":"2019-01-23T18:42:27.868Z"},"relationships":{"configurable":{"data":{"id":"ws-cejaVXJhZHZQsKiR","type":"workspaces"},"links":{"related":"/api/v2/organizations/JohnBoero/workspaces/hashihang-eks"}}},"links":{"self":"/api/v2/vars/var-QsqT9gg4wMBYf1D6"}}]}[jboero@z600 hashihang-eks]$ 
[jboero@z600 hashihang-eks]$ jq . vars
{
  "data": [
    {
      "id": "var-QjAhj6U6hqnTZWVx",
      "type": "vars",
      "attributes": {
        "key": "AWS_ACCESS_KEY_ID",
        "value": null,
        "sensitive": true,
        "category": "env",
        "hcl": false,
        "created-at": "2018-06-21T17:26:22.570Z"
      },
      "relationships": {
        "configurable": {
          "data": {
            "id": "ws-cejaVXJhZHZQsKiR",
            "type": "workspaces"
          },
          "links": {
            "related": "/api/v2/organizations/JohnBoero/workspaces/hashihang-eks"
          }
        }
      },
      "links": {
        "self": "/api/v2/vars/var-QjAhj6U6hqnTZWVx"
      }
    },
    {
      "id": "var-87P1xEnPcte82TRC",
      "type": "vars",
      "attributes": {
        "key": "AWS_SECRET_ACCESS_KEY",
        "value": null,
        "sensitive": true,
        "category": "env",
        "hcl": false,
        "created-at": "2018-06-21T17:26:22.566Z"
      },
      "relationships": {
        "configurable": {
          "data": {
            "id": "ws-cejaVXJhZHZQsKiR",
            "type": "workspaces"
          },
          "links": {
            "related": "/api/v2/organizations/JohnBoero/workspaces/hashihang-eks"
          }
        }
      },
      "links": {
        "self": "/api/v2/vars/var-87P1xEnPcte82TRC"
      }
    },
    {
      "id": "var-Tx4urno2KBFt5ywR",
      "type": "vars",
      "attributes": {
        "key": "CONFIRM_DESTROY",
        "value": "1",
        "sensitive": false,
        "category": "env",
        "hcl": false,
        "created-at": "2018-06-22T10:25:51.346Z"
      },
      "relationships": {
        "configurable": {
          "data": {
            "id": "ws-cejaVXJhZHZQsKiR",
            "type": "workspaces"
          },
          "links": {
            "related": "/api/v2/organizations/JohnBoero/workspaces/hashihang-eks"
          }
        }
      },
      "links": {
        "self": "/api/v2/vars/var-Tx4urno2KBFt5ywR"
      }
    },
    {
      "id": "var-QsqT9gg4wMBYf1D6",
      "type": "vars",
      "attributes": {
        "key": "k8s_cluster_size",
        "value": null,
        "sensitive": true,
        "category": "terraform",
        "hcl": false,
        "created-at": "2019-01-23T18:42:27.868Z"
      },
      "relationships": {
        "configurable": {
          "data": {
            "id": "ws-cejaVXJhZHZQsKiR",
            "type": "workspaces"
          },
          "links": {
            "related": "/api/v2/organizations/JohnBoero/workspaces/hashihang-eks"
          }
        }
      },
      "links": {
        "self": "/api/v2/vars/var-QsqT9gg4wMBYf1D6"
      }
    }
  ]
}
```
