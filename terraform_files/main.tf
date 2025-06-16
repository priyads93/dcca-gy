terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.47.0"
    }

    random = {
      source  = "hashicorp/random"
      version = "~> 3.6.1"
    }

    tls = {
      source  = "hashicorp/tls"
      version = "~> 4.0.5"
    }

    cloudinit = {
      source  = "hashicorp/cloudinit"
      version = "~> 2.3.4"
    }
  }

  required_version = "~> 1.3"
}

provider "aws" {
  region = "ap-southeast-1"
}

resource "aws_ecr_repository" "diameterimage" {
  name = "diameterimage"
}

resource "aws_acm_certificate" "private_cert" {
  domain_name               = "gemba.diameterserver.com"
  certificate_authority_arn = "arn:aws:acm-pca:ap-southeast-1:519139470698:certificate-authority/899d9848-e135-4a36-a178-2e4deba0b60a"
  key_algorithm         = "RSA_2048"

  options {
    certificate_transparency_logging_preference = "DISABLED"
  }

}

resource "aws_acm_certificate" "private_cert_client" {
  domain_name               = "gemba.diameterclient.com"
  certificate_authority_arn = "arn:aws:acm-pca:ap-southeast-1:519139470698:certificate-authority/899d9848-e135-4a36-a178-2e4deba0b60a"
  key_algorithm         = "RSA_2048"

  options {
    certificate_transparency_logging_preference = "DISABLED"
  }

}

# Filter out local zones, which are not currently supported 
# with managed node groups
data "aws_availability_zones" "available" {
  filter {
    name   = "opt-in-status"
    values = ["opt-in-not-required"]
  }
}

locals {
  cluster_name = "gemba-dev-eks"
}


module "vpc" {
  source  = "terraform-aws-modules/vpc/aws"
  version = "5.8.1"

  name = "gemba-dev-vpc"

  cidr = "10.0.0.0/16"
  azs  = slice(data.aws_availability_zones.available.names, 0, 3)

  private_subnets = ["10.0.1.0/24", "10.0.2.0/24", "10.0.3.0/24"]
  public_subnets  = ["10.0.4.0/24", "10.0.5.0/24", "10.0.6.0/24"]

  enable_nat_gateway   = true
  single_nat_gateway   = true
  enable_dns_hostnames = true

  public_subnet_tags = {
    "kubernetes.io/role/elb" = 1
  }

  private_subnet_tags = {
    "kubernetes.io/role/internal-elb" = 1
  }
}

module "eks" {
  source  = "terraform-aws-modules/eks/aws"
  version = "20.8.5"

  cluster_name    = local.cluster_name
  cluster_version = "1.32"

  cluster_endpoint_public_access           = true
  enable_cluster_creator_admin_permissions = true

  cluster_addons = {
    aws-ebs-csi-driver = {
      service_account_role_arn = module.irsa-ebs-csi.iam_role_arn
    }
  }

  vpc_id     = module.vpc.vpc_id
  subnet_ids = module.vpc.private_subnets

  eks_managed_node_group_defaults = {
    ami_type = "AL2_x86_64"
  }

  eks_managed_node_groups = {
    one = {
      name = "node-group-1"

      instance_types = ["t3.small"]

      min_size     = 1
      max_size     = 1
      desired_size = 1
    }   

    two = {
      name = "node-group-2"

      instance_types = ["t3.small"]

      min_size     = 1
      max_size     = 2
      desired_size = 2

      force_update_version = true
    }   

    three = {
      name = "node-group-3"

      instance_types = ["t3.small"]

      min_size     = 1
      max_size     = 2
      desired_size = 2
    }   
  }
}


# --- DB Subnet Group ---
resource "aws_db_subnet_group" "rds_subnet_group" {
  name       = "ubm-rds-subnet-group"
  subnet_ids = module.vpc.private_subnets

  tags = {
    Name = "ubm-rds-subnet-group"
  }
}

resource "aws_security_group" "rds_sg" {
  name        = "rds-postgres-sg"
  description = "Security group for RDS PostgreSQL"
  vpc_id      = module.vpc.vpc_id

  tags = {
    Name = "ubm-rds-sg"
  }
}

# --- RDS Instance ---
resource "aws_db_instance" "ubm_db" {
  allocated_storage       = 20
  engine                  = "postgres"
  engine_version          = "17.4"
  instance_class          = "db.t3.micro"
  db_name                 = "ubm_postgresql"
  username                = "postgres"
  password                = "password123"
  skip_final_snapshot     = true
  # Enable encryption at rest
  storage_encrypted = true
  kms_key_id        = "arn:aws:kms:ap-southeast-1:519139470698:key/e6074514-f462-4418-be7a-71ee4e966b6d"
  db_subnet_group_name    = aws_db_subnet_group.rds_subnet_group.name
  vpc_security_group_ids  = [aws_security_group.rds_sg.id]
  publicly_accessible     = false
  multi_az                = false
  availability_zone       = "ap-southeast-1a"  
}

resource "aws_vpc_security_group_ingress_rule" "allow_postgres_from_eks" {
  security_group_id = aws_security_group.rds_sg.id
  description        = "Allow Postgres access from EKS nodes"
  from_port          = 5432
  to_port            = 5432
  ip_protocol        = "tcp"
  referenced_security_group_id = module.eks.node_security_group_id
}


resource "aws_vpc_security_group_egress_rule" "allow_all_outbound" {
  security_group_id = module.eks.cluster_security_group_id
  description        = "Allow all outbound traffic"
  ip_protocol        = "-1"
  cidr_ipv4          = "0.0.0.0/0"
}



resource "aws_vpc_security_group_egress_rule" "rds_allow_all" {
  security_group_id = aws_security_group.rds_sg.id
  ip_protocol       = "-1"
  cidr_ipv4         = "0.0.0.0/0"
  description       = "Allow all outbound"
}




# https://aws.amazon.com/blogs/containers/amazon-ebs-csi-driver-is-now-generally-available-in-amazon-eks-add-ons/ 
data "aws_iam_policy" "ebs_csi_policy" {
  arn = "arn:aws:iam::aws:policy/service-role/AmazonEBSCSIDriverPolicy"
}

module "irsa-ebs-csi" {
  source  = "terraform-aws-modules/iam/aws//modules/iam-assumable-role-with-oidc"
  version = "5.39.0"

  create_role                   = true
  role_name                     = "AmazonEKSTFEBSCSIRole-${module.eks.cluster_name}"           
  provider_url                  = module.eks.oidc_provider
  role_policy_arns              = [data.aws_iam_policy.ebs_csi_policy.arn]
  oidc_fully_qualified_subjects = ["system:serviceaccount:kube-system:ebs-csi-controller-sa"]
}

output "cluster_endpoint" {
  description = "Endpoint for EKS control plane"
  value       = module.eks.cluster_endpoint
}

output "cluster_security_group_id" {
  description = "Security group ids attached to the cluster control plane"
  value       = module.eks.cluster_security_group_id
}

output "region" {
  description = "AWS region"
  value       = "ap-southeast-1"
}

output "cluster_name" {
  description = "Kubernetes Cluster Name"
  value       = module.eks.cluster_name
}