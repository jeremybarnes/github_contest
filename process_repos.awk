# Script to process the raw repos and turn it into a parseable file

$1 == ":description:" {
    valid = 1;
    desc = $0;
    gsub("^ *:description: ", "", desc);
    gsub("\"", "\\\"", desc);
}

$1 == ":name:" {
    name = $2;
}

$1 == ":owner:" {
    owner = $2;
}

/---/ && valid {
    printf("%s/%s:\"%s\"\n", owner, name, desc);
    valid = 0;
}