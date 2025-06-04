terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 4.16"
    }
  }
}

provider "aws" {
  region = "ap-southeast-2"
}

resource "aws_ecr_repository" "diameterimage" {
  name = "diameterimage"
}