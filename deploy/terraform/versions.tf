# ---------------------------------------------------------------------------
# Provider and version constraints.
#
# Pinned with `~>` rather than left open: an unpinned provider means a
# `terraform init` months from now can produce a different plan against
# unchanged code, which is the opposite of what infrastructure-as-code is for.
#
# The backend block is commented out because remote state configuration is
# deployment-specific and cannot be committed usefully. Configure it before the
# first apply — local state for shared infrastructure means no locking, no
# history, and one laptop failure away from losing the ability to manage it.
# ---------------------------------------------------------------------------
terraform {
  required_version = ">= 1.6.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.40"
    }
    random = {
      source  = "hashicorp/random"
      version = "~> 3.6"
    }
  }

  # backend "s3" {
  #   bucket       = "my-terraform-state"
  #   key          = "realtime-chat/terraform.tfstate"
  #   region       = "eu-west-1"
  #   encrypt      = true
  #   use_lockfile = true   # S3-native locking (Terraform >= 1.10)
  # }
}

provider "aws" {
  region = var.aws_region

  default_tags {
    # Applied to every taggable resource, so cost allocation and ownership never
    # depend on remembering to tag individually.
    tags = {
      Project     = var.project_name
      Environment = var.environment
      ManagedBy   = "terraform"
      Repository  = "realtime-chat"
    }
  }
}
