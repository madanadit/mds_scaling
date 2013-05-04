LD_FLAGS = -lhdfs -ljvm

all : ./testLibHdfs

./testLibHdfs : testLibHdfs.c
	gcc -g testLibHdfs.c -I${JAVA_HOME}/include -I${JAVA_HOME}/include/linux -I${HADOOP_HOME}/include -lhdfs -ljvm -o testLibHdfs

clean :
	rm ./testLibHdfs
