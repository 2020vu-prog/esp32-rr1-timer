#!/bin/bash
echo $BRANCH_NAME   
export AWS_ACCOUNT=''
export AWS_BUCKET=''
if [[ "$BRANCH_NAME" == "go.rr1.us" ]]; then
        export AWS_ACCOUNT='654654501960'
        export AWS_BUCKET='svelte-static-20240420233732488700000002'
fi


if [[ "$BRANCH_NAME" == "dev" ]]; then
        export AWS_ACCOUNT='178616746534'  
        export AWS_BUCKET='svelte-static-20221204164305433000000002'
fi
 echo "AWS_ACCOUNT=$AWS_ACCOUNT" >> $GITHUB_ENV
 echo "AWS_BUCKET=$AWS_BUCKET" >> $GITHUB_ENV