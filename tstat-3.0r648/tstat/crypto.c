#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include "panon.h"
#include "tstat.h"
#include "crypto.h"
#include "base64.h"

/* Redefine the initial hash size to a large value, to avoid/reduce the automatic rehashing */

#define HASH_INITIAL_NUM_BUCKETS 131072      /* initial number of buckets        */
#define HASH_INITIAL_NUM_BUCKETS_LOG2 17     /* lg2 of initial number of buckets */

/* Use the Bernstein hash function */
#define HASH_FUNCTION HASH_BER

#include "uthash.h" /* Include the generic hash table */

#ifndef MAX_CRYPTO_CACHE_SIZE
#define MAX_CRYPTO_CACHE_SIZE HASH_INITIAL_NUM_BUCKETS
#endif

#define KEY_SIZE 32

struct key_hashT {
  in_addr_t key;
  in_addr_t cpan_addr;
  UT_hash_handle hh;
};

struct key_hashT *address_hash = NULL;

void add_address(in_addr_t src, in_addr_t cpan_addr) {
    struct key_hashT *s,*tmp_entry;

    s = (struct key_hashT *)malloc(sizeof(struct key_hashT));
    s->key = src;
    s->cpan_addr = cpan_addr;
    HASH_ADD_INT( address_hash, key, s );  /* id: name of key field */
    
    /* Manage the hash as a LRU cache */
    if (HASH_COUNT(address_hash) > MAX_CRYPTO_CACHE_SIZE)
      {
        HASH_ITER(hh, address_hash, s, tmp_entry)
         {
            // prune the first entry (loop is based on insertion order so this deletes the oldest item)
           HASH_DELETE(hh, address_hash, s);
           free(s);
           break;
	 }
      }
}

struct key_hashT *find_address(in_addr_t src) {
    struct key_hashT *s;

    HASH_FIND_INT( address_hash, &src, s );  /* s: output pointer */
    
    /* Manage the hash as a LRU cache */
    if (s) {
      // remove it (so the subsequent add will throw it on the front of the list)
      HASH_DELETE(hh, address_hash, s);
      HASH_ADD(hh,address_hash, key, sizeof(s->key), s);
      return s;
     }
    return s;
}

int crypto_total_hit,crypto_total_insert,crypto_total_miss;

void initialize_crypto(int key_source, char *value, char *basenamedir)
{
  FILE *fp;
  char *key;
  char *keyfile;
  char *enc_key;
  char date[50];
  char line[121];
  char *decoded_key = NULL;
  int flen,i;
  in_addr_t ip1,ip2;
  
  key = (char *) malloc(sizeof(char) * KEY_SIZE);
  memset(key,0,KEY_SIZE*sizeof(char));

  switch (key_source)
   {
    case CPKEY_RANDOM:
      fprintf(fp_stdout,"Generating random key (might take some time)...\n");
      fp = fopen("/dev/random", "r");

      if (fp==NULL)
      {
	fprintf(fp_stderr,"Error opening /dev/random. Exiting\n");
	exit(1);
      }
      
      if (fread(key,1, KEY_SIZE, fp) != KEY_SIZE)
      {
	fprintf(fp_stderr,"Cannot generate random key\n");
	exit(1);
      }
      fprintf(fp_stdout,"... done\n");
      
      fclose(fp);
      break;
    case CPKEY_FILE:
      if (value==NULL)
       {
	 fprintf(fp_stderr,"Invalid key file name\n");
	 exit(1);
       }
       
      fprintf(fp_stdout,"Reading plain text key from file %s ...\n",value);
      fp = fopen(value, "r");

      if (fp==NULL)
      {
	fprintf(fp_stderr,"Error opening file %s. Exiting\n",value);
	exit(1);
      }
      
      if (fgets(line,120,fp) == NULL)
      {
	fprintf(fp_stderr,"Cannot read plain text key from file\n");
	exit(1);
      }
      fprintf(fp_stdout,"... done\n");
      
      if (line[strlen(line)-1]=='\n')
       {
	 line[strlen(line)-1]='\0';
       }

      // printf(">>%s<<\n",line);
      
      if (strlen(line)<1)
       {
	 fprintf(fp_stderr,"Plain text key empty. Exiting\n");
	 exit(1);
       }
      decoded_key=strdup(line);
      flen = strlen(decoded_key);

      if (flen>KEY_SIZE)
       {
	 fprintf(fp_stdout,"Key is too long: using only the first %d bytes\n",KEY_SIZE);
         memcpy(key,decoded_key,KEY_SIZE*sizeof(char));
       }
      else
       {
         memcpy(key,decoded_key,flen*sizeof(char));
       }
	
      if (flen<KEY_SIZE)
       {
	 fprintf(fp_stdout,"Key shorter than %d bytes: padding with zeros\n",KEY_SIZE);
       }

      if (debug>2)
       {
         for (i=0;i<KEY_SIZE;i++)
          {
 	    fprintf(fp_stdout,"%hhx ",(char)key[i]);
          }
         fprintf(fp_stdout,"\n");
       }
       
      fclose(fp);
      break;
    case CPKEY_FILE64:
      if (value==NULL)
       {
	 fprintf(fp_stderr,"Invalid key file name\n");
	 exit(1);
       }
       
      fprintf(fp_stdout,"Reading Base64 encoded key from file %s ...\n",value);
      fp = fopen(value, "r");

      if (fp==NULL)
      {
	fprintf(fp_stderr,"Error opening file %s. Exiting\n",value);
	exit(1);
      }
      
      if (fgets(line,120,fp) == NULL)
      {
	fprintf(fp_stderr,"Cannot read Base64 encoded key from file\n");
	exit(1);
      }
      fprintf(fp_stdout,"... done\n");
      
      if (line[strlen(line)-1]=='\n')
       {
	 line[strlen(line)-1]='\0';
       }

      // printf(">>%s<<\n",line);
      
      /* The line is supposed to be Base64 encoded */
      /* This version of unbase64() does trust the input and does not
         implements any check on the input format. Invalid characters are
         considered as 0 ('A') */
      decoded_key = (char *)unbase64(line,strlen(line),&flen);
      if (decoded_key==NULL)
       {
	 fprintf(fp_stderr,"Base64 decoding failed. Exiting\n");
	 exit(1);
       }

      if (flen>KEY_SIZE)
       {
	 fprintf(fp_stdout,"Key is too long: using only the first %d bytes\n",KEY_SIZE);
         memcpy(key,decoded_key,KEY_SIZE*sizeof(char));
       }
      else
       {
         memcpy(key,decoded_key,flen*sizeof(char));
       }
	
      if (flen<KEY_SIZE)
       {
	 fprintf(fp_stdout,"Key shorter than %d bytes: padding with zeros\n",KEY_SIZE);
       }

      if (debug>2)
       {
         for (i=0;i<KEY_SIZE;i++)
          {
 	    fprintf(fp_stdout,"%hhx ",(char)key[i]);
          }
         fprintf(fp_stdout,"\n");
       }
       
      fclose(fp);
      break;
    case CPKEY_CLI:
      if (value==NULL)
       {
	 fprintf(fp_stderr,"Invalid key\n");
	 exit(1);
       }
       
      fprintf(fp_stdout,"Using plain text key from command line\n");

      // printf(">>%s<<\n",value);
      
      // duplicate the string
      decoded_key=strdup(value);
      flen = strlen(decoded_key);

      if (flen>KEY_SIZE)
       {
	 fprintf(fp_stdout,"Key is too long: using only the first %d bytes\n",KEY_SIZE);
         memcpy(key,decoded_key,KEY_SIZE*sizeof(char));
       }
      else
       {
         memcpy(key,decoded_key,flen*sizeof(char));
       }
	
      if (flen<KEY_SIZE)
       {
	 fprintf(fp_stdout,"Key shorter than %d bytes: padding with zeros\n",KEY_SIZE);
       }

      if (debug>2)
       {
         for (i=0;i<KEY_SIZE;i++)
          {
 	    fprintf(fp_stdout,"%hhx ",(char)key[i]);
          }
         fprintf(fp_stdout,"\n");
       }
      
      break;
    default:
      fprintf(fp_stderr,"Invalid key source\n");
      exit(1);
      break;
   }

  encrypt_init(key,KEY_SIZE);

  strftime (date, 49, "%Y_%m_%d_%H_%M", localtime (&current_time.tv_sec));
  keyfile = (char *)malloc(strlen(basenamedir)+strlen("/CPanKey_")+strlen(date)+2);
  
  strcpy(keyfile,basenamedir);
  strcat(keyfile,"/CPanKey_");
  strcat(keyfile,date);

  enc_key = base64(key,KEY_SIZE,&flen);
  
  fp = fopen(keyfile,"w");
  if (fp!=NULL)
   {
     fprintf(fp,"%s\n",enc_key);
   }
  else
   {
     fprintf(fp_stderr,"Error opening %s. CPan key not stored\n",keyfile);
   }
  fclose(fp);
  
  /* Insert one address (0.0.0.0) just to inizialize the hash to the full size */
  ip1 = inet_addr("0.0.0.0");
  ip2 = htonl(encrypt_ip(htonl(ip1)));
  add_address(ip1,ip2);
  
  crypto_total_hit = 0;
  crypto_total_insert = 1;
  crypto_total_miss = 0;

  if (enc_key!=NULL) free(enc_key);
  if (keyfile!=NULL) free(keyfile);
  if (decoded_key!=NULL) free(decoded_key);

  return;

}

void encrypt_init(char *key, int keysize)
{
  char cryptopan_key[32];
  memset(cryptopan_key,0,sizeof(cryptopan_key));

  memcpy(cryptopan_key,key,keysize<sizeof(cryptopan_key)?keysize:sizeof(cryptopan_key));
  panon_init(cryptopan_key);
}

uint32_t encrypt_ip(uint32_t orig_addr) 
{
  return cpp_anonymize(orig_addr);
}

void store_crypto_ip(struct in_addr *address)
{
  in_addr_t ip_entry;
  struct key_hashT *entry;
  
  entry = find_address(address->s_addr);

  if (entry == NULL)
   {
     ip_entry = htonl(encrypt_ip(htonl(address->s_addr)));
     add_address(address->s_addr,ip_entry);
     crypto_total_insert++;
   }
  else
  {
    crypto_total_hit++;
  }

}

in_addr_t retrieve_crypto_ip(struct in_addr *address)
{
  in_addr_t ip_entry;
  struct key_hashT *entry;
  
  entry = find_address(address->s_addr);

  if (entry==NULL)
   {
     ip_entry = htonl(encrypt_ip(htonl(address->s_addr)));
     add_address(address->s_addr,ip_entry);
     crypto_total_insert++;
     crypto_total_miss++;

     return ip_entry;
   }
  else
  {
    return entry->cpan_addr;
  }
}

char *HostNameEncrypted(ipaddr ipaddress)
{
  char *adr;
  ipaddr encrypted;

#ifdef SUPPORT_IPV6
  if (ADDR_ISV6 (&ipaddress))
    {
      adr = HostAddr (ipaddress);
      return (adr);
    }
  else
#endif
    {
      encrypted.un.ip4.s_addr = retrieve_crypto_ip(&(ipaddress.un.ip4));
      adr = HostName(encrypted);
      return (adr);
    }
}