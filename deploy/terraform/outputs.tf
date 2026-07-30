# ---------------------------------------------------------------------------
# Outputs.
#
# Endpoints and ARNs a human or a CI pipeline needs after an apply. Note that no
# output exposes a secret *value* — only the ARN needed to fetch it. An output
# marked `sensitive` is still written to state and can be read with
# `terraform output -json`, so the safest design is not to output it at all.
# ---------------------------------------------------------------------------

output "vpc_id" {
  description = "VPC identifier."
  value       = aws_vpc.main.id
}

output "public_subnet_ids" {
  description = "Public subnet ids (load balancer, NAT)."
  value       = aws_subnet.public[*].id
}

output "private_subnet_ids" {
  description = "Private subnet ids (application instances)."
  value       = aws_subnet.private[*].id
}

output "database_subnet_ids" {
  description = "Database subnet ids (RDS, ElastiCache)."
  value       = aws_subnet.database[*].id
}

output "load_balancer_dns_name" {
  description = "Public DNS name of the load balancer. Point a CNAME here."
  value       = aws_lb.main.dns_name
}

output "load_balancer_zone_id" {
  description = "Hosted zone id of the load balancer, for a Route 53 alias record."
  value       = aws_lb.main.zone_id
}

output "application_url" {
  description = "Base URL for the API."
  value = var.domain_name != "" ? "https://${var.domain_name}" : (
    var.acm_certificate_arn != "" ? "https://${aws_lb.main.dns_name}" : "http://${aws_lb.main.dns_name}"
  )
}

output "api_docs_url" {
  description = "Swagger UI location."
  value = "${var.domain_name != "" ? "https://${var.domain_name}" : "http://${aws_lb.main.dns_name}"}/docs"
}

output "instance_ids" {
  description = "Application instance ids. Connect with: aws ssm start-session --target <id>"
  value       = aws_instance.app[*].id
}

output "database_endpoint" {
  description = "RDS endpoint (host:port)."
  value       = aws_db_instance.main.endpoint
}

output "database_address" {
  description = "RDS hostname."
  value       = aws_db_instance.main.address
}

output "redis_primary_endpoint" {
  description = "ElastiCache primary endpoint. Also carries cross-instance WebSocket fan-out."
  value       = aws_elasticache_replication_group.main.primary_endpoint_address
}

output "redis_reader_endpoint" {
  description = "ElastiCache reader endpoint."
  value       = aws_elasticache_replication_group.main.reader_endpoint_address
}

output "database_secret_arn" {
  description = <<-EOT
    ARN of the Secrets Manager secret holding the database credentials. Read it
    with:
      aws secretsmanager get-secret-value --secret-id <arn> --query SecretString --output text
  EOT
  value       = aws_secretsmanager_secret.db.arn
}

output "application_secret_arn" {
  description = "ARN of the Secrets Manager secret holding JWT_SECRET."
  value       = aws_secretsmanager_secret.app.arn
}

output "application_log_group" {
  description = "CloudWatch log group for application logs."
  value       = aws_cloudwatch_log_group.app.name
}

output "security_group_ids" {
  description = "Security group ids by tier."
  value = {
    alb      = aws_security_group.alb.id
    app      = aws_security_group.app.id
    database = aws_security_group.database.id
    cache    = aws_security_group.cache.id
  }
}

output "cost_notes" {
  description = "The parts of this stack that dominate the bill, and how to reduce them."
  value = {
    nat_gateway = var.enable_nat_gateway ? (
      var.single_nat_gateway
      ? "1 NAT gateway (~USD 32/mo + data). Not HA — the gateway's AZ is a single point of failure for egress."
      : "${length(local.azs)} NAT gateways (~USD 32/mo each + data). Set single_nat_gateway=true in non-production."
    ) : "No NAT gateway. Private subnets have no outbound internet access."
    rds   = var.db_multi_az ? "RDS Multi-AZ: roughly double the single-AZ cost. Required for any real availability target." : "RDS single-AZ. A failover means downtime and potential data loss."
    redis = "ElastiCache with ${var.redis_replica_count} replica(s). At least one is needed for automatic failover; Redis carries WebSocket fan-out, not just cache."
  }
}
