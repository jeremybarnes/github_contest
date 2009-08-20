#!/bin/bash --norc

# cat download/repos.txt | sed 's!.*:\([^:]*\)/.*!\1!g' | sort | uniq | xargs -n1 ./get_user.sh > authors.txt

curl http://github.com/api/v2/yaml/user/show/$1 2>/dev/null | \
awk -v username=$1 'BEGIN { printf("%s:", username); } /following_count:/ { printf("%d,", $2); } /id:/ { printf("%d,", $2); } /followers_count:/ { printf("%d,", $2); } /created_at:/ { printf("%s", $2)} END { printf("\n"); }'
sleep 0.7s
