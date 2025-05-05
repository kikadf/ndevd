


#define	NDEVD_ATTACH_EVENT	"device-attach"
#define	NDEVD_DETACH_EVENT	"device-detach"

#define NDEVD_SOCKET "/var/run/ndevd.socket"
#define NDEVD_MSG 65

struct ndevd_msg {
    char event[NDEVD_MSG];
    char device[NDEVD_MSG];
};
