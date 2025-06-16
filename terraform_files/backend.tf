terraform {
  backend "s3" {
    bucket         	   = "ubm-terraform-state"
    key              	 = "dev/eks/terraform.tfstate"
    region         	   = "ap-southeast-1"
    encrypt        	   = true
    dynamodb_table = "ubm-terraform-lock-table"
  }
}