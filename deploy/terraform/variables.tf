# ---------------------------------------------------------------------------
# Input variables.
#
# Every variable carries a type and a description, and anything with a
# correctness or security implication carries a validation rule. A validation
# failure at plan time is enormously cheaper than discovering the same mistake
# from a half-created VPC.
# ---------------------------------------------------------------------------

variable "aws_region" {
  description = "AWS region to deploy into."
  type        = string
  default     = "eu-west-1"
}

variable "project_name" {
  description = "Name prefix for all resources."
  type        = string
  default     = "realtime-chat"

  validation {
    # Many AWS resource names disallow underscores and uppercase; catching it here
    # avoids a confusing mid-apply failure.
    condition     = can(regex("^[a-z][a-z0-9-]{2,24}$", var.project_name))
    error_message = "project_name must be 3-25 lowercase alphanumeric or hyphen characters, starting with a letter."
  }
}

variable "environment" {
  description = "Deployment environment."
  type        = string
  default     = "production"

  validation {
    condition     = contains(["development", "staging", "production"], var.environment)
    error_message = "environment must be one of: development, staging, production."
  }
}

# --- Networking ------------------------------------------------------------

variable "vpc_cidr" {
  description = "CIDR block for the VPC."
  type        = string
  default     = "10.40.0.0/16"

  validation {
    condition     = can(cidrhost(var.vpc_cidr, 0))
    error_message = "vpc_cidr must be a valid IPv4 CIDR block."
  }
}

variable "availability_zone_count" {
  description = "Number of availability zones to span."
  type        = number
  default     = 2

  validation {
    # RDS Multi-AZ and a load balancer both require subnets in at least two AZs.
    condition     = var.availability_zone_count >= 2 && var.availability_zone_count <= 3
    error_message = "availability_zone_count must be 2 or 3 (RDS Multi-AZ and the ALB both require at least two)."
  }
}

variable "enable_nat_gateway" {
  description = <<-EOT
    Provision a NAT gateway so private subnets have outbound internet access
    (needed for package installs and container pulls). This is the single largest
    fixed cost in this stack — roughly USD 32/month per gateway plus data
    processing — so it is a deliberate switch rather than an implicit default.
    Set false only if every dependency is reachable via VPC endpoints.
  EOT
  type        = bool
  default     = true
}

variable "single_nat_gateway" {
  description = <<-EOT
    Route all private subnets through one NAT gateway instead of one per AZ.
    Cheaper, but the gateway's AZ becomes a single point of failure for outbound
    traffic. Acceptable in staging; not in production.
  EOT
  type        = bool
  default     = false
}

# --- Application instances -------------------------------------------------

variable "instance_type" {
  description = "EC2 instance type for application servers."
  type        = string
  default     = "t3.small"
}

variable "instance_count" {
  description = "Number of application instances behind the load balancer."
  type        = number
  default     = 2

  validation {
    condition     = var.instance_count >= 1 && var.instance_count <= 20
    error_message = "instance_count must be between 1 and 20."
  }
}

variable "ssh_allowed_cidrs" {
  description = <<-EOT
    CIDR blocks permitted to reach SSH. Empty by default, which means no SSH
    ingress at all — use AWS Systems Manager Session Manager instead (the IAM role
    created here already grants it). Opening 22 to 0.0.0.0/0 is rejected below.
  EOT
  type        = list(string)
  default     = []

  validation {
    condition     = !contains(var.ssh_allowed_cidrs, "0.0.0.0/0")
    error_message = "Refusing to open SSH to the entire internet. Use Session Manager, or list specific CIDRs."
  }
}

# --- Database --------------------------------------------------------------

variable "db_instance_class" {
  description = "RDS instance class."
  type        = string
  default     = "db.t4g.micro"
}

variable "db_allocated_storage" {
  description = "Initial RDS storage in GiB."
  type        = number
  default     = 20
}

variable "db_max_allocated_storage" {
  description = "Upper bound for RDS storage autoscaling in GiB. Set equal to db_allocated_storage to disable."
  type        = number
  default     = 100
}

variable "db_name" {
  description = "Initial PostgreSQL database name."
  type        = string
  default     = "realtime_chat"
}

variable "db_username" {
  description = "PostgreSQL master username."
  type        = string
  default     = "chat"

  validation {
    # PostgreSQL reserves these; RDS rejects them at create time.
    condition     = !contains(["admin", "postgres", "rdsadmin", "root"], lower(var.db_username))
    error_message = "db_username must not be a reserved name (admin, postgres, rdsadmin, root)."
  }
}

variable "db_multi_az" {
  description = "Run RDS with a synchronous standby in a second AZ. Doubles cost; required for any real availability target."
  type        = bool
  default     = true
}

variable "db_backup_retention_days" {
  description = "Days of automated RDS backups. Zero disables backups entirely."
  type        = number
  default     = 14

  validation {
    condition     = var.db_backup_retention_days >= 1
    error_message = "Backup retention must be at least 1 day. Disabling backups also disables point-in-time recovery."
  }
}

variable "db_deletion_protection" {
  description = "Prevent `terraform destroy` from deleting the database."
  type        = bool
  default     = true
}

# --- Cache -----------------------------------------------------------------

variable "redis_node_type" {
  description = "ElastiCache node type."
  type        = string
  default     = "cache.t4g.micro"
}

variable "redis_replica_count" {
  description = <<-EOT
    Read replicas per shard. At least 1 is required for automatic failover, and
    Redis here is not merely a cache — it carries cross-instance WebSocket
    fan-out, so losing it degrades message delivery, not just latency.
  EOT
  type        = number
  default     = 1
}

# --- TLS -------------------------------------------------------------------

variable "acm_certificate_arn" {
  description = <<-EOT
    ARN of an ACM certificate for the HTTPS listener. When empty, only an HTTP
    listener is created — acceptable for a first bring-up, never for production,
    since JWTs would then travel in clear text.
  EOT
  type        = string
  default     = ""
}

variable "domain_name" {
  description = "Public hostname for the service (used in outputs and tags)."
  type        = string
  default     = ""
}
