# Kubernetes manifests

Production-ready manifests for the realtime-chat backend. Plain YAML with no
templating engine, so they can be read and applied directly; if you need
environments, layer Kustomize overlays on top rather than parameterising these.

## Files

| File | Purpose |
| --- | --- |
| `namespace.yaml` | Namespace, with Pod Security Admission labels |
| `configmap.yaml` | Non-secret configuration (one key per environment variable) |
| `secret.yaml` | **Template only** — credential keys and their shape |
| `deployment.yaml` | Workload, probes, security context, resources |
| `service.yaml` | ServiceAccount, ClusterIP Service, headless Service |
| `redis.yaml` | Redis Deployment + Service — the cluster bus transport. **Required whenever `replicas` > 1**: without it the pods fall back to a no-op bus and WebSocket delivery becomes silently partial. Single replica, no persistence; swap for a managed service in production |
| `ingress.yaml` | TLS, WebSocket timeouts, body size, `/metrics` deny |
| `hpa.yaml` | HorizontalPodAutoscaler (CPU + memory) |
| `pdb.yaml` | PodDisruptionBudget |
| `networkpolicy.yaml` | Default-deny egress with an allowlist |

## Apply order

```bash
kubectl apply -f deploy/k8s/namespace.yaml
```

```bash
kubectl -n realtime-chat apply -f deploy/k8s/
```

## Before you go live

1. **Replace every `REPLACE_ME`** in `secret.yaml`. Better still, delete the file
   and source secrets from AWS Secrets Manager via External Secrets — the
   Terraform in `deploy/terraform` provisions that secret for you.
   `Config::validate()` refuses to boot in production with a weak `JWT_SECRET`, so
   a placeholder fails loudly at startup rather than quietly.
2. **Pin the image tag.** `:latest` is in `deployment.yaml` for readability; use a
   digest or an immutable tag so a rollback is meaningful.
3. **Set the ConfigMap checksum annotation.** Without it, editing the ConfigMap
   does not restart pods and your change silently does not take effect:
   ```bash
   kubectl -n realtime-chat get cm realtime-chat-config -o yaml | sha256sum
   ```
4. **Narrow `CORS_ALLOWED_ORIGINS`** from `*` to your actual front-end origins.
5. **Replace `chat.example.com`** in `ingress.yaml`.
6. **Decide on attachment storage.** `deployment.yaml` mounts an `emptyDir` for
   uploads, which is per-pod scratch. With more than one replica an attachment
   uploaded to one pod is invisible to the others — use object storage (S3) or an
   RWX volume for anything durable.

## Why the manifests look the way they do

Several choices here are specific to a WebSocket service and are worth
understanding before changing them.

**Three separate health probes.** Liveness never touches a dependency: a liveness
probe that queried PostgreSQL would restart every replica during a database blip,
converting a recoverable dependency problem into a full outage. Readiness *is*
allowed to fail — it takes the pod out of rotation without killing it. The startup
probe covers the migration window so a slow start is not mistaken for a hang.

**`maxUnavailable: 0` and a slow scale-down.** Every terminating pod severs its
WebSocket connections, and those clients all reconnect simultaneously. Rollouts
add a pod before removing one, and the HPA scales down at most one pod every two
minutes with a five-minute stabilisation window.

**`CLUSTER_ENABLED=true` is not optional at `replicas: 3`.** WebSocket connections
are pinned to the pod that accepted them, so without the Redis Pub/Sub fan-out a
message persisted on pod A never reaches a recipient connected to pod B. The
symptom is intermittent, user-visible message loss that does not reproduce on a
single replica. `Config::validate()` rejects `CLUSTER_ENABLED` without
`REDIS_ENABLED` for the same reason.

**No CPU limit.** CFS throttling at the limit introduces latency spikes, which is
the one thing a real-time service cannot afford. Memory *is* limited, so a leak is
contained rather than taking down the node.

**`sessionAffinity: None`.** A WebSocket is already sticky once established, and
the cluster bus means REST calls need not land on the same pod as the socket.
Affinity would only skew load distribution.

## Verifying a deployment

```bash
kubectl -n realtime-chat rollout status deployment/realtime-chat --timeout=180s
```

```bash
kubectl -n realtime-chat exec deploy/realtime-chat -- wget -qO- localhost:8080/health/ready
```

The readiness body reports each dependency individually plus the cluster state, so
a failure names the culprit:

```json
{
  "status": "ready",
  "checks": { "database": "up", "cache": "up", "workers": "up",
              "scheduler": "up", "bootstrap": "complete" },
  "cluster": { "node_id": "realtime-chat-7d4f...", "distributed": true }
}
```

If `cluster.distributed` is `false` while running more than one replica, fan-out
is broken — check `REDIS_ENABLED`, `CLUSTER_ENABLED`, and that the image was built
with `-DRTC_WITH_REDIS=ON`.
