# ---------------------------------------------------------------------------
# Security groups.
#
# Every rule is a *separate* aws_vpc_security_group_*_rule resource rather than an
# inline ingress/egress block. Inline blocks are authoritative: Terraform removes
# any rule it does not know about, which silently reverts an emergency manual
# change. Separate resources also give each rule its own description in the
# console, so the reason for a hole is visible where someone will look for it.
#
# The tiers reference each other by security group id, not CIDR. That means the
# rules stay correct when subnets change, and "only the application may reach the
# database" is expressed structurally rather than by IP arithmetic.
# ---------------------------------------------------------------------------

# --- Load balancer ---------------------------------------------------------

resource "aws_security_group" "alb" {
  name        = "${local.name_prefix}-alb"
  description = "Public entry point: HTTP/HTTPS from the internet"
  vpc_id      = aws_vpc.main.id

  tags = { Name = "${local.name_prefix}-alb" }

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_vpc_security_group_ingress_rule" "alb_https" {
  security_group_id = aws_security_group.alb.id
  description       = "HTTPS from the internet"
  cidr_ipv4         = "0.0.0.0/0"
  from_port         = 443
  to_port           = 443
  ip_protocol       = "tcp"
}

resource "aws_vpc_security_group_ingress_rule" "alb_http" {
  security_group_id = aws_security_group.alb.id
  # Kept open only to serve the permanent redirect to HTTPS; the listener itself
  # returns 301 and never forwards to a target.
  description = "HTTP from the internet (redirected to HTTPS)"
  cidr_ipv4   = "0.0.0.0/0"
  from_port   = 80
  to_port     = 80
  ip_protocol = "tcp"
}

resource "aws_vpc_security_group_egress_rule" "alb_to_app" {
  security_group_id            = aws_security_group.alb.id
  description                  = "Forward to application instances"
  referenced_security_group_id = aws_security_group.app.id
  from_port                    = 8080
  to_port                      = 8080
  ip_protocol                  = "tcp"
}

# --- Application -----------------------------------------------------------

resource "aws_security_group" "app" {
  name        = "${local.name_prefix}-app"
  description = "Application instances: traffic from the load balancer only"
  vpc_id      = aws_vpc.main.id

  tags = { Name = "${local.name_prefix}-app" }

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_vpc_security_group_ingress_rule" "app_from_alb" {
  security_group_id = aws_security_group.app.id
  # Referencing the ALB's group (not a CIDR) is what makes the instances
  # unreachable except through the load balancer, regardless of subnet layout.
  description                  = "Application traffic from the load balancer"
  referenced_security_group_id = aws_security_group.alb.id
  from_port                    = 8080
  to_port                      = 8080
  ip_protocol                  = "tcp"
}

# SSH is opt-in and empty by default; Session Manager is the intended access path
# (the instance role already grants it), which leaves no inbound port to attack.
resource "aws_vpc_security_group_ingress_rule" "app_ssh" {
  count = length(var.ssh_allowed_cidrs)

  security_group_id = aws_security_group.app.id
  description       = "SSH from an explicitly allowed network"
  cidr_ipv4         = var.ssh_allowed_cidrs[count.index]
  from_port         = 22
  to_port           = 22
  ip_protocol       = "tcp"
}

# Outbound stays open: instances need package updates, container registries, ACME
# challenges and OTLP export. Egress filtering belongs at a NAT/firewall layer, not
# here, where it would be a long and fragile allowlist.
resource "aws_vpc_security_group_egress_rule" "app_all" {
  security_group_id = aws_security_group.app.id
  description       = "All outbound (updates, registries, telemetry)"
  cidr_ipv4         = "0.0.0.0/0"
  ip_protocol       = "-1"
}

# --- Database --------------------------------------------------------------

resource "aws_security_group" "database" {
  name        = "${local.name_prefix}-database"
  description = "PostgreSQL: reachable only from application instances"
  vpc_id      = aws_vpc.main.id

  tags = { Name = "${local.name_prefix}-database" }

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_vpc_security_group_ingress_rule" "database_from_app" {
  security_group_id            = aws_security_group.database.id
  description                  = "PostgreSQL from application instances"
  referenced_security_group_id = aws_security_group.app.id
  from_port                    = 5432
  to_port                      = 5432
  ip_protocol                  = "tcp"
}

# No egress rule at all. RDS does not initiate outbound connections, and the
# absence of one (combined with the database tier's NAT-free route table) means
# there is no path out of the data tier.

# --- Cache -----------------------------------------------------------------

resource "aws_security_group" "cache" {
  name        = "${local.name_prefix}-cache"
  description = "Redis: reachable only from application instances"
  vpc_id      = aws_vpc.main.id

  tags = { Name = "${local.name_prefix}-cache" }

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_vpc_security_group_ingress_rule" "cache_from_app" {
  security_group_id            = aws_security_group.cache.id
  description                  = "Redis from application instances"
  referenced_security_group_id = aws_security_group.app.id
  from_port                    = 6379
  to_port                      = 6379
  ip_protocol                  = "tcp"
}
