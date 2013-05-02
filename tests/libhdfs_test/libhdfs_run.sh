#!/bin/bash

export CLASSPATH=/usr/lib/jvm/java-6-openjdk/jre/lib/jsse.jar:/usr/lib/jvm/java-6-openjdk/jre/lib/:$(find ../../hadoop-0.23.1-lib/ -name "*.jar" | tr '\n' ':')
./testLibHdfs

