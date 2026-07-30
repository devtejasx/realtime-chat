# ---------------------------------------------------------------------------
# Redis (ElastiCache).
#
# Redis is not optional in a multi-instance deployment of this service. Beyond
# caching and rate limiting it carries the Pub/Sub fan-out that delivers WebSocket
# events across instances (see rtc::realtime::RedisClusterBus). Losing it does not
# merely slow things down — messages stop reaching recipients connected to other
# instances. It is therefore configured for availability, not as a disposable cache.
# ---------------------------------------------------------------------------

resource "aws_elasticache_subnet_group" "main" {
  name        = "${local.name_prefix}-cache"
  description = "Database tier subnets for ${local.name_prefix} cache"
  subnet_ids  = aws_subnet.database[*].id

  tags = { Name = "${local.name_prefix}-cache-subnet-group" }
}

resource "aws_elasticache_parameter_group" "main" {
  name        = "${local.name_prefix}-redis7"
  family      = "redis7"
  description = "Redis parameters for ${local.name_prefix}"

  # allkeys-lru, not the default noeviction.
  #
  # Under memory pressure noeviction makes writes fail, which would break rate
  # limiting and presence rather than degrading them. Every key this service stores
  # in Redis is reconstructible from PostgreSQL, so evicting the least-recently-used
  # key is strictly better than refusing to write.
  parameter {
    name  = "maxmemory-policy"
    value = "allkeys-lru"
  }

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_cloudwatch_log_group" "redis_slow" {
  name              = "/aws/elasticache/${local.name_prefix}/slow-log"
  retention_in_days = 14

  tags = { Name = "${local.name_prefix}-redis-slow-log" }
}

resource "aws_elasticache_replication_group" "main" {
  replication_group_id = "${local.name_prefix}-redis"
  description          = "Cache, rate limiting and WebSocket fan-out for ${local.name_prefix}"

  engine         = "redis"
  engine_version = "7.1"
  node_type      = var.redis_node_type
  port           = 6379

  # One shard with replicas. Redis Pub/Sub in cluster mode requires clients to
  # handle sharded pub/sub, which redis-plus-plus does not do transparently — so a
  # single shard keeps the fan-out correct. Vertical scaling plus replicas is ample
  # for this workload.
  num_cache_clusters = 1 + var.redis_replica_count

  automatic_failover_enabled = var.redis_replica_count > 0
  multi_az_enabled           = var.redis_replica_count > 0

  subnet_group_name  = aws_elasticache_subnet_group.main.name
  security_group_ids = [aws_security_group.cache.id]
  parameter_group_name = aws_elasticache_parameter_group.main.name

  at_rest_encryption_enabled = true
  # In-transit encryption is left off deliberately, and this is a real trade-off:
  # enabling it requires every client to speak TLS, and the redis-plus-plus build
  # used here is not configured for it. Traffic stays inside the VPC's database
  # tier, reachable only from the application security group. Turn this on — and
  # rebuild redis-plus-plus with TLS — if your compliance posture requires
  # encryption inside the VPC.
  transit_encryption_enabled = false

  # Snapshots. Redis holds no data that cannot be rebuilt, but a snapshot makes a
  # cold restart warm instead of a thundering herd against PostgreSQL.
  snapshot_retention_limit = 3
  snapshot_window          = "02:00-03:00"
  maintenance_window       = "sun:05:30-sun:06:30"

  auto_minor_version_upgrade = true
  apply_immediately          = false

  log_delivery_configuration {
    destination      = aws_cloudwatch_log_group.redis_slow.name
    destination_type = "cloudwatch-logs"
    log_format       = "json"
    log_type         = "slow-log"
  }

  tags = { Name = "${local.name_prefix}-redis" }
}
