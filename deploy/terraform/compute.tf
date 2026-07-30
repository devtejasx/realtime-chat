# ---------------------------------------------------------------------------
# Application instances and the load balancer.
# ---------------------------------------------------------------------------

data "aws_ami" "ubuntu" {
  most_recent = true
  owners      = ["099720109477"] # Canonical

  filter {
    name   = "name"
    values = ["ubuntu/images/hvm-ssd-gp3/ubuntu-jammy-22.04-amd64-server-*"]
  }

  filter {
    name   = "virtualization-type"
    values = ["hvm"]
  }
}

# --- Launch template -------------------------------------------------------

resource "aws_launch_template" "app" {
  name_prefix   = "${local.name_prefix}-app-"
  image_id      = data.aws_ami.ubuntu.id
  instance_type = var.instance_type

  iam_instance_profile {
    name = aws_iam_instance_profile.app.name
  }

  vpc_security_group_ids = [aws_security_group.app.id]

  metadata_options {
    # IMDSv2 required. IMDSv1 lets any SSRF bug in the application read the
    # instance's IAM credentials with a single unauthenticated GET; requiring the
    # token exchange closes that.
    http_tokens                 = "required"
    http_endpoint               = "enabled"
    http_put_response_hop_limit = 1 # blocks containers from reaching IMDS
    instance_metadata_tags      = "enabled"
  }

  block_device_mappings {
    device_name = "/dev/sda1"

    ebs {
      volume_size           = 30
      volume_type           = "gp3"
      encrypted             = true
      delete_on_termination = true
    }
  }

  monitoring {
    enabled = true
  }

  user_data = base64encode(templatefile("${path.module}/templates/user_data.sh.tftpl", {
    project_name    = var.project_name
    environment     = var.environment
    aws_region      = var.aws_region
    db_secret_arn   = aws_secretsmanager_secret.db.arn
    app_secret_arn  = aws_secretsmanager_secret.app.arn
    redis_endpoint  = aws_elasticache_replication_group.main.primary_endpoint_address
    log_group_name  = aws_cloudwatch_log_group.app.name
  }))

  tag_specifications {
    resource_type = "instance"
    tags          = { Name = "${local.name_prefix}-app" }
  }

  tags = { Name = "${local.name_prefix}-app-template" }

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_instance" "app" {
  count = var.instance_count

  launch_template {
    id      = aws_launch_template.app.id
    version = "$Latest"
  }

  # Round-robin across AZs so instance_count = 2 spans both.
  subnet_id = aws_subnet.private[count.index % length(aws_subnet.private)].id

  tags = {
    Name = "${local.name_prefix}-app-${count.index + 1}"
    Role = "application"
  }

  lifecycle {
    # A new AMI release should not silently replace running instances on the next
    # unrelated apply. Roll instances deliberately (or move to an Auto Scaling
    # Group with an instance-refresh policy).
    ignore_changes = [ami]
  }
}

# --- Load balancer ---------------------------------------------------------

resource "aws_lb" "main" {
  name               = "${local.name_prefix}-alb"
  load_balancer_type = "application"
  internal           = false
  security_groups    = [aws_security_group.alb.id]
  subnets            = aws_subnet.public[*].id

  # Longer than the application's 90s WebSocket heartbeat timeout, so the
  # *application* decides a connection is dead rather than the load balancer
  # severing a healthy idle socket.
  idle_timeout = 3600

  enable_deletion_protection = var.environment == "production"
  drop_invalid_header_fields = true
  enable_http2               = true

  tags = { Name = "${local.name_prefix}-alb" }
}

resource "aws_lb_target_group" "app" {
  name        = "${local.name_prefix}-app"
  port        = 8080
  protocol    = "HTTP"
  vpc_id      = aws_vpc.main.id
  target_type = "instance"

  health_check {
    enabled = true
    # /health/ready, not /health/live: the load balancer must stop sending traffic
    # to an instance whose dependencies are unavailable. Liveness would keep an
    # instance in rotation while every request it received failed.
    path                = "/health/ready"
    protocol            = "HTTP"
    matcher             = "200"
    interval            = 15
    timeout             = 5
    healthy_threshold   = 2
    unhealthy_threshold = 3
  }

  # Long enough for in-flight requests to complete and for WebSocket peers to be
  # closed cleanly; matches terminationGracePeriodSeconds in the K8s manifests.
  deregistration_delay = 45

  stickiness {
    # Off for the same reason as in Kubernetes: a WebSocket is already pinned to one
    # instance, and the Redis fan-out means REST calls need not land on the same
    # one. Stickiness would only unbalance the load.
    enabled = false
    type    = "lb_cookie"
  }

  tags = { Name = "${local.name_prefix}-app-tg" }

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_lb_target_group_attachment" "app" {
  count = var.instance_count

  target_group_arn = aws_lb_target_group.app.arn
  target_id        = aws_instance.app[count.index].id
  port             = 8080
}

# HTTP listener: redirects, never forwards. Serving the API over plain HTTP would
# put bearer tokens on the wire in clear text.
resource "aws_lb_listener" "http" {
  load_balancer_arn = aws_lb.main.arn
  port              = 80
  protocol          = "HTTP"

  default_action {
    type = var.acm_certificate_arn != "" ? "redirect" : "forward"

    # Only populated when a certificate exists.
    dynamic "redirect" {
      for_each = var.acm_certificate_arn != "" ? [1] : []
      content {
        port        = "443"
        protocol    = "HTTPS"
        status_code = "HTTP_301"
      }
    }

    # Fallback for a first bring-up without a certificate. Not for production —
    # see the acm_certificate_arn description.
    target_group_arn = var.acm_certificate_arn != "" ? null : aws_lb_target_group.app.arn
  }
}

resource "aws_lb_listener" "https" {
  count = var.acm_certificate_arn != "" ? 1 : 0

  load_balancer_arn = aws_lb.main.arn
  port              = 443
  protocol          = "HTTPS"
  # TLS 1.2 minimum, forward secrecy required. The AWS default policy still permits
  # TLS 1.0/1.1.
  ssl_policy      = "ELBSecurityPolicy-TLS13-1-2-2021-06"
  certificate_arn = var.acm_certificate_arn

  default_action {
    type             = "forward"
    target_group_arn = aws_lb_target_group.app.arn
  }
}
