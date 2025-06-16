terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

provider "aws" {
  region = "ap-southeast-1"
}



resource "aws_acmpca_certificate_authority" "root_ca" {
  type = "ROOT"
  certificate_authority_configuration {
    key_algorithm     = "RSA_2048"
    signing_algorithm = "SHA256WITHRSA"

    subject {
      common_name = "gemba.diameterserver.com"
      organization = "Gemba"
    }
  }
}


output "root_ca_arn" {
  value = aws_acmpca_certificate_authority.root_ca.arn
}

