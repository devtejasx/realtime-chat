# AWS Deployment

A reference architecture for running realtime-chat on AWS. Scripts live in
`deploy/aws/`.

## Reference architecture

```
                Route 53 (DNS)
                     │
             ┌───────▼────────┐
             │  ALB (HTTPS)   │  TLS via ACM, health check → /health/ready
             └───────┬────────┘
        ┌────────────┼────────────┐
   ┌────▼────┐  ┌────▼────┐   (Auto Scaling Group, ≥2 AZs)
   │  EC2 #1 │  │  EC2 #2 │   Docker: nginx + app
   └────┬────┘  └────┬────┘
        └──────┬──────┘
        ┌──────▼───────┐     ┌──────────────┐
        │ RDS Postgres │     │ ElastiCache  │
        │ (Multi-AZ)   │     │ Redis        │
        └──────────────┘     └──────────────┘
```

- **ALB** terminates TLS (ACM certificate) and load-balances across instances;
  target-group health check → `/health/ready`. (You can also let Nginx on each
  instance terminate TLS via Let's Encrypt; ALB is recommended for multi-instance.)
- **Auto Scaling Group** across ≥2 Availability Zones for HA.
- **RDS PostgreSQL** (Multi-AZ) as the managed database.
- **ElastiCache Redis** as the shared cache/session/presence store.
- **S3** (optional) for attachment storage via a future `IFileStorage` backend.

## Provisioning outline

1. **Networking** — a VPC with public subnets (ALB) and private subnets (EC2,
   RDS, ElastiCache).
2. **RDS PostgreSQL** — Multi-AZ, in the private subnets; note the endpoint.
3. **ElastiCache Redis** — cluster/replication group in the private subnets.
4. **Security Groups**
   - ALB SG: inbound `443` (and `80` for redirect) from the internet.
   - EC2 SG: inbound from the ALB SG only (app/Nginx ports); SSH from your IP.
   - RDS SG: inbound `5432` from the EC2 SG only.
   - ElastiCache SG: inbound `6379` from the EC2 SG only.
5. **IAM role** for EC2 with least privilege: read the app secret from SSM
   Parameter Store / Secrets Manager, and (optionally) S3 access for uploads and
   CloudWatch Logs.
6. **EC2 / ASG** — Ubuntu 24.04; use `deploy/aws/user-data.sh` as launch
   user-data to install Docker and start the stack.
7. **Elastic IP** (single-instance) or the **ALB DNS** (multi-instance) in
   Route 53.

## Secrets

Do **not** bake secrets into the image or user-data. Store `JWT_SECRET`,
`DB_PASSWORD`, and the RDS/ElastiCache endpoints in **SSM Parameter Store**
(SecureString) or **Secrets Manager**, and fetch them into
`/opt/realtime-chat/.env` on boot (a placeholder is shown in `user-data.sh`):

```bash
aws ssm get-parameter --with-decryption --name /realtime-chat/env \
  --query Parameter.Value --output text > /opt/realtime-chat/.env
```

## Deploy / update / rollback

On an instance (or via SSM Run Command / your CD tool):

```bash
cd /opt/realtime-chat
./deploy/aws/deploy.sh main        # pull, build, migrate, recreate, verify
./deploy/aws/rollback.sh           # revert to the previous revision
./deploy/aws/health-check.sh       # verify liveness + readiness
```

For zero-downtime, run migrations once (`server --migrate`) then roll instances
one at a time behind the ALB.

## Database & cache notes

- Point `DB_HOST` at the **RDS endpoint**, `REDIS_URL` at the **ElastiCache
  endpoint** (`tcp://<endpoint>:6379`), and set `REDIS_ENABLED=true`.
- Enable RDS automated backups + snapshots; verify restores periodically (see
  [Deployment.md](Deployment.md) disaster-recovery section).
- Size `DB_POOL_SIZE` so `instances × pool` stays under the RDS
  `max_connections`; add RDS Proxy / PgBouncer at scale.

## Observability on AWS

- Ship container stdout (JSON logs) to **CloudWatch Logs**.
- Scrape `/metrics` with a Prometheus agent in the VPC, or use the CloudWatch
  agent; alarm on the expressions in [Monitoring.md](Monitoring.md).
