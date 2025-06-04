terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

provider "aws" {
  region = "ap-southeast-2"
}

resource "aws_acm_certificate" "private_cert" {
  domain_name               = "gemba.diameterserver.com"
  certificate_authority_arn = "arn:aws:acm-pca:ap-southeast-2:519139470698:certificate-authority/a303d2d9-96cc-43a5-893d-4c8c6fef5184"
  key_algorithm         = "RSA_2048"

  options {
    certificate_transparency_logging_preference = "DISABLED"
  }

}

resource "aws_acm_certificate" "private_cert_client" {
  domain_name               = "gemba.diameterclient.com"
  certificate_authority_arn = "arn:aws:acm-pca:ap-southeast-2:519139470698:certificate-authority/a303d2d9-96cc-43a5-893d-4c8c6fef5184"
  key_algorithm         = "RSA_2048"

  options {
    certificate_transparency_logging_preference = "DISABLED"
  }

}