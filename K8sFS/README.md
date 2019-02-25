# KubernetesFS
Simple browseable CRUD FUSE client for Kubernetes .  It's written in C++ with FUSE v28, libCurl, libJSONcpp.  Currently draft PoC with no guarantees.

# Running KubernetesFS
Assuming a local K8s instance is running, use environment variables just like Vault CLI:

```
$ export KUBE_APISERVER=http://localhost:8080
$ export KUBE_TOKEN=[YOUR TOKEN]
$ k8sfs -o direct_io /mnt/k8s (or your mount path)
```

For debugging, best results via single threaded DEBUG build:
```
$ k8sfs -d -s -o direct_io /mnt/vault
```

# Stopping KubernetesFS
To stop k8sfs running, or reset it in event of an issue, just dismount the fs:
```
fusermount -u /mnt/vault (or your mount path)
```

# Browsing Vault
Kubernetes resources can be browsed like a local filesystem.  

```
$ tree /mnt/k8s/
/mnt/k8s/
├── default
│   ├── cronjobs
│   ├── daemonsets
│   ├── deployments
│   │   ├── bash
│   │   └── hashiconf
│   ├── jobs
│   ├── pods
│   ├── replicasets
│   │   ├── bash-68d67dd789
│   │   └── hashiconf-6689f577d8
│   └── services
│       ├── hashiconf
│       ├── kubernetes
│       └── kubernetes-cockpit
├── kube-public
│   ├── cronjobs
│   ├── daemonsets
│   ├── deployments
│   ├── jobs
│   ├── pods
│   ├── replicasets
│   └── services
└── kube-system
    ├── cronjobs
    ├── daemonsets
    ├── deployments
    ├── jobs
    ├── pods
    ├── replicasets
    └── services

24 directories, 7 files
$ 
```

Manifests can be read/written.  Note that if invalid JSON or manifest details are provided, an I/O error will reject your write/save.  Annoyingly some API endpoints don't have compatible read/write options, and some details must be removed if you plan to read + edit + write resources.
```
$ cat /mnt/k8s/default/services/hashiconf 
{
  "kind": "Service",
  "apiVersion": "v1",
  "metadata": {
    "name": "hashiconf",
    "namespace": "default",
    "selfLink": "/api/v1/namespaces/default/services/hashiconf",
    "uid": "12997d3b-3464-11e9-81de-78e7d1c5d96f",
    "resourceVersion": "10983",
    "creationTimestamp": "2019-02-19T16:33:22Z",
    "labels": {
      "run": "hashiconf"
    }
  },
  "spec": {
    "ports": [
      {
        "protocol": "TCP",
        "port": 81,
        "targetPort": 80,
        "nodePort": 31277
      }
    ],
    "selector": {
      "run": "hashiconf"
    },
    "clusterIP": "10.254.222.128",
    "type": "NodePort",
    "sessionAffinity": "None",
    "externalTrafficPolicy": "Cluster"
  },
  "status": {
    "loadBalancer": {
      
    }
  }
}
```                                                                                               