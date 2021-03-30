FROM scratch

MAINTAINER John Boero <jboero@hashicorp.com>
COPY openapifs .
COPY vault /
CMD ["./openapifs -s -o direct_io /yourpath"]
