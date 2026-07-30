# ---------------------------------------------------------------------------
# IAM for the application instances.
#
# Least privilege throughout: the instance role can read exactly the two secrets
# this deployment creates, write its own logs and metrics, and be managed through
# Session Manager. Nothing else. In particular there is no wildcard
# `secretsmanager:GetSecretValue` on `*`, which is the most common over-grant in
# this pattern and would let a compromised instance read every secret in the
# account.
# ---------------------------------------------------------------------------

resource "aws_iam_role" "app" {
  name        = "${local.name_prefix}-app"
  description = "Instance role for ${local.name_prefix} application servers"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect    = "Allow"
      Principal = { Service = "ec2.amazonaws.com" }
      Action    = "sts:AssumeRole"
    }]
  })

  tags = { Name = "${local.name_prefix}-app-role" }
}

resource "aws_iam_instance_profile" "app" {
  name = "${local.name_prefix}-app"
  role = aws_iam_role.app.name
}

# --- Secrets access --------------------------------------------------------

resource "aws_iam_role_policy" "app_secrets" {
  name = "${local.name_prefix}-secrets-read"
  role = aws_iam_role.app.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Sid    = "ReadOwnSecretsOnly"
      Effect = "Allow"
      Action = [
        "secretsmanager:GetSecretValue",
        "secretsmanager:DescribeSecret",
      ]
      # Scoped to these two ARNs, not "*".
      Resource = [
        aws_secretsmanager_secret.db.arn,
        aws_secretsmanager_secret.app.arn,
      ]
    }]
  })
}

# --- Observability ---------------------------------------------------------

resource "aws_cloudwatch_log_group" "app" {
  name              = "/aws/ec2/${local.name_prefix}/application"
  retention_in_days = 30

  tags = { Name = "${local.name_prefix}-app-logs" }
}

resource "aws_iam_role_policy" "app_logs" {
  name = "${local.name_prefix}-logs-write"
  role = aws_iam_role.app.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Sid    = "WriteApplicationLogs"
        Effect = "Allow"
        Action = [
          "logs:CreateLogStream",
          "logs:PutLogEvents",
          "logs:DescribeLogStreams",
        ]
        Resource = "${aws_cloudwatch_log_group.app.arn}:*"
      },
      {
        Sid    = "PublishCustomMetrics"
        Effect = "Allow"
        # PutMetricData cannot be scoped by resource; it is constrained by namespace
        # instead, so the instance cannot overwrite metrics in other namespaces.
        Action   = ["cloudwatch:PutMetricData"]
        Resource = "*"
        Condition = {
          StringEquals = {
            "cloudwatch:namespace" = var.project_name
          }
        }
      },
    ]
  })
}

# --- Session Manager -------------------------------------------------------

# Grants shell access without an inbound SSH port, a key pair to distribute, or a
# bastion host to maintain — and every session is logged in CloudTrail. This is why
# ssh_allowed_cidrs defaults to empty.
resource "aws_iam_role_policy_attachment" "app_ssm" {
  role       = aws_iam_role.app.name
  policy_arn = "arn:aws:iam::aws:policy/AmazonSSMManagedInstanceCore"
}
