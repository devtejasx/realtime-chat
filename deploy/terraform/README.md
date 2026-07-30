# Terraform — AWS infrastructure

Provisions the full stack: VPC, EC2 application instances, RDS PostgreSQL,
ElastiCache Redis, IAM, security groups and an Application Load Balancer.

## Layout

| File | Contents |
| --- | --- |
| `versions.tf` | Provider pins, default tags, remote-state backend (commented) |
| `variables.tf` | Inputs, with validation on anything security-relevant |
| `network.tf` | VPC, three subnet tiers, routing, NAT, S3 endpoint, flow logs |
| `security_groups.tf` | Per-tier groups, referencing each other by id |
| `iam.tf` | Instance role, scoped secret access, Session Manager |
| `rds.tf` | PostgreSQL, parameter group, generated credentials |
| `elasticache.tf` | Redis replication group and parameters |
| `compute.tf` | Launch template, instances, ALB, target group, listeners |
| `outputs.tf` | Endpoints, ARNs, cost notes |
| `templates/user_data.sh.tftpl` | Instance bootstrap |

## Usage

```bash
cd deploy/terraform && terraform init
```

```bash
terraform plan -out=tfplan
```

```bash
terraform apply tfplan
```

Then read the connection details:

```bash
terraform output application_url && terraform output -json cost_notes
```

## Before the first apply

1. **Configure remote state.** Uncomment the `backend "s3"` block in
   `versions.tf`. Local state for shared infrastructure means no locking, no
   history, and one laptop failure away from losing the ability to manage it.
2. **Provide an ACM certificate.** Without `acm_certificate_arn` only an HTTP
   listener is created, and JWTs would travel in clear text. Fine for a first
   bring-up; never for production.
3. **Review the cost switches.** `enable_nat_gateway` and `db_multi_az` dominate
   the bill. `terraform output cost_notes` explains the trade-offs.

## Design decisions worth knowing

**Secrets are generated, never passed in.** `random_password` creates the database
password and `JWT_SECRET`, and both go straight into Secrets Manager. A
`-var db_password=...` would land in shell history, in CI logs, and in the plan
output. Instances read the secrets at boot using the instance role, so no
credential appears in user data — which matters because user data is readable by
anything that can reach the instance metadata service.

**Three subnet tiers, and the database tier has no internet route at all.** Its
route table contains only the local VPC route, so there is no outbound path from a
compromised database host. Combined with security groups that reference each other
by id rather than by CIDR, "only the application may reach the database" is
structural rather than a matter of getting IP arithmetic right.

**Security group rules are separate resources.** Inline `ingress`/`egress` blocks
are authoritative: Terraform deletes any rule it does not know about, silently
reverting an emergency manual change. Separate resources also carry per-rule
descriptions, so the reason a port is open is visible in the console.

**Redis is configured for availability, not as a disposable cache.** It carries the
Pub/Sub fan-out that delivers WebSocket events across instances, so losing it stops
messages reaching recipients on other instances rather than merely slowing things
down. Hence `automatic_failover_enabled`, `multi_az_enabled`, and
`maxmemory-policy = allkeys-lru` (the default `noeviction` would make writes *fail*
under memory pressure, breaking rate limiting and presence).

**IMDSv2 is required** with a hop limit of 1. IMDSv1 lets any SSRF bug in the
application read the instance's IAM credentials with one unauthenticated GET.

**SSH is closed by default.** `ssh_allowed_cidrs` is empty and validation rejects
`0.0.0.0/0`; access is via Session Manager, which the instance role already grants.
No inbound port, no key distribution, no bastion, and every session in CloudTrail.

**The ALB health check targets `/health/ready`, not `/health/live`.** Readiness
fails when a dependency is unavailable, which correctly removes the instance from
rotation. Using liveness would keep an instance receiving traffic while every
request it served failed.

**`idle_timeout = 3600`.** Above the application's 90-second WebSocket heartbeat
timeout, so the application decides when a connection is dead rather than the load
balancer severing a healthy idle socket.

## What this does not do

* **No Auto Scaling Group.** `aws_instance` with a count is easier to read and to
  reason about for a fixed fleet. A production deployment that needs elastic
  capacity should convert `compute.tf` to an ASG with an instance-refresh policy —
  the launch template is already in place for exactly that.
* **No Route 53 records.** DNS is usually managed outside the application stack;
  `load_balancer_dns_name` and `load_balancer_zone_id` are exported for an alias
  record.
* **No S3 bucket for attachments.** The application writes to a local directory by
  default. Multi-instance durable attachments need object storage — the S3 VPC
  endpoint is already provisioned for it.
* **No `transit_encryption_enabled` on Redis.** Enabling it requires every client to
  speak TLS, and the redis-plus-plus build used here is not configured for it.
  Traffic stays within the database tier, reachable only from the application
  security group. Turn it on — and rebuild redis-plus-plus with TLS — if your
  compliance posture requires encryption inside the VPC.

## Destroying

```bash
terraform destroy
```

`db_deletion_protection` defaults to `true`, so the database survives a `destroy`
until you explicitly set it to `false`. `skip_final_snapshot` is `false`, so a
deletion always leaves a recoverable snapshot behind.
