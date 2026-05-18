#!/bin/bash
echo $BRANCH_NAME   
export AWS_ACCOUNT=''
if [[ "$BRANCH_NAME" == "main" ]]; then
        export AWS_ACCOUNT='1.2.3'  
           
fi


if [[ "$BRANCH_NAME" == "dev" ]]; then
        export AWS_ACCOUNT='178616746534'  

fi
 echo "AWS_ACCOUNT=$AWS_ACCOUNT" >> $GITHUB_ENV