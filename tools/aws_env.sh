#!/bin/bash
echo $BRANCH_NAME   
export AWS_ACCOUNT=''
export AWS_BUCKET=''
if [[ "$BRANCH_NAME" == "main" ]]; then
        export AWS_ACCOUNT='1.2.3'  
           
fi


if [[ "$BRANCH_NAME" == "dev" ]]; then
        export AWS_ACCOUNT='178616746534'  
        export AWS_BUCKET='svelte-static-20221204164305433000000002'

fi
 echo "AWS_ACCOUNT=$AWS_ACCOUNT" >> $GITHUB_ENV
 echo "AWS_BUCKET=$AWS_BUCKET" >> $GITHUB_ENV