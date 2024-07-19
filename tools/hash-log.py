#!/usr/bin/env python3
import sys
import hashlib
import ipaddress
from cachetools import cached

help="""
It reads a Tstat log from stdin and outputs to stdout.
If an IP address is encrypted with Crypto-PAn (and the corresponding column indicates it),
    it is replaced with a hash using SHA-256.
The tool supports 6to4, so that, for IPv6 addresses,
    it extracts the IPv4 address and computes the hash on it.
The user must specify the <log> argument. This tool supports Tstat logs:
- log_tcp_complete
- log_tcp_nocomplete
- log_udp_complete
- log_dns_complete
- log_mm_complete
- log_periodic_complete
- log_periodic_udp_complete
The hash is salted using the <salt> argument.
The tool is a noop if <active> is 0.
"""

# Hash Lenght: SHA 256 is truncated to HASH_LEN Hex digits
HASH_LEN=32

def main():

    # Check and parse args
    if len(sys.argv)!=4:
        print(f"Usage: {sys.argv[0]} log salt active{{1|0}}", file=sys.stderr)
        print(help, file=sys.stderr)
        exit(1)

    log=sys.argv[1]
    salt=sys.argv[2]
    active=sys.argv[3]

    # Get the fields to operate on, based on the log type
    if log == "log_tcp_complete" or log == "log_tcp_nocomplete":
        c_ip_col=1 -1
        s_ip_col=15 -1
        c_iscrypto_col=40 -1
        s_iscrypto_col=41 -1
    elif log == "log_udp_complete":
        c_ip_col=1 -1
        s_ip_col=10 -1
        c_iscrypto_col=8 -1
        s_iscrypto_col=17 -1
    elif log == "log_dns_complete":
        c_ip_col=1 -1
        s_ip_col=5 -1
        c_iscrypto_col=4 -1
        s_iscrypto_col=8 -1
    elif log == "log_mm_complete":
        c_ip_col=4 -1
        s_ip_col=10 -1
        c_iscrypto_col=7 -1
        s_iscrypto_col=13 -1
    elif log == "log_tcp_periodic":
        c_ip_col=1 -1
        s_ip_col=3 -1
        c_iscrypto_col=45 -1
        s_iscrypto_col=46 -1
    elif log == "log_udp_periodic":
        c_ip_col=1 -1
        s_ip_col=5 -1
        c_iscrypto_col=4 -1
        s_iscrypto_col=8 -1
    else:
        print(f"Unknown log type: {log}", file=sys.stderr)
        exit(1)

    # Process the lines  
    for line in sys.stdin:

        # Get the fields
        fields = line.split(" ")
        c_ip = fields[c_ip_col]
        s_ip = fields[s_ip_col]
        c_iscrypto = fields[c_iscrypto_col]
        s_iscrypto = fields[s_iscrypto_col]

        # Anonymize if needed
        if active == "1":
            c_ip = anon(c_ip,c_iscrypto,salt)
            s_ip = anon(s_ip,s_iscrypto,salt)

        # Write the line on stdout
        fields[c_ip_col] = c_ip
        fields[s_ip_col] = s_ip
        print(" ".join(fields), end="")

@cached(cache ={}) 
def anon(ip,is_crypto,salt):

    # Operate only on already anonymized addresses
    if is_crypto.strip() == "1":
        # For IPv4, hash the address with salt
        if "." in ip:
            return "IPv4:" + hashlib.sha256( f"{salt}:{ip}".encode('utf-8')).hexdigest()[:HASH_LEN]
        # For IPv6, get the IPv4 from using the 6to4 mechanism
        elif ":" in ip:
            ip = ipaddress.ip_address(ip.strip()).exploded
            w1 = ip.split(":")[2]
            w2 = ip.split(":")[3]
            b1 =  int(w1[0:2],16)
            b2 =  int(w1[2:4],16)
            b3 =  int(w2[0:2],16)
            b4 =  int(w2[2:4],16)
            ip_virt = f"{b1}.{b2}.{b3}.{b4}"
            hashed_p1 = hashlib.sha256( f"{salt}:{ip_virt}".encode('utf-8')).hexdigest()[:HASH_LEN]

            host = ":".join(ip.split(":")[4:8])
            hashed_p2 = hashlib.sha256( f"{salt}:{host}".encode('utf-8')).hexdigest()[:HASH_LEN]

            hashed = "IPv6:" + hashed_p1 + ":" + hashed_p2
            return hashed
        # Notice in case of problems
        else:
            print(f"Unknown IP: {ip}", file=sys.stderr)
            return "UNK:" + hashlib.sha256( f"{salt}:{ip}".encode('utf-8')).hexdigest()[:HASH_LEN]
    # Return original address if not encypted
    else:
        return ip

if __name__ == "__main__":
    main()
