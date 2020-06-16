FROM scratch

MAINTAINER John Boero <jboero@hashicorp.com>
COPY vaultfs .
COPY vault /
CMD ["./vaultfs -s -o direct_io /vault"]
