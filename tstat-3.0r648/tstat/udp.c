/*
 *
 * Copyright (c) 2001
 *	Politecnico di Torino.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * For bug report and other information please visit Tstat site:
 * http://tstat.polito.it
 *
 * Tstat is deeply based on TCPTRACE. The authors would like to thank
 * Shawn Ostermann for the development of TCPTRACE.
 *
*/


#include "tstat.h"
#include "tcpL7.h"


/* Dichiarazione dei mutex per gestione dei thread */
extern pthread_mutex_t utp_lock_mutex;
extern pthread_mutex_t flow_close_started_mutex;
extern pthread_mutex_t flow_close_cond_mutex;
extern pthread_cond_t flow_close_cond;
extern Bool threaded;

extern struct L4_bitrates L4_bitrate;

/* locally global variables */
static int packet_count = 0;
static int search_count = 0;



/* provided globals  */
int num_udp_pairs = -1;		/* how many pairs we've allocated */
u_long udp_trace_count = 0;
udp_pair **utp = NULL;		/* array of pointers to allocated pairs */


/* local routine definitions */
static udp_pair *NewUTP (struct ip *, struct udphdr *);
static udp_pair *FindUTP (struct ip *, struct udphdr *, int *);


extern unsigned long int fcount;
extern Bool warn_MAX_;
extern unsigned long int f_UDP_count;

#ifdef CHECK_UDP_DUP
Bool
dup_udp_check (struct ip *pip, struct udphdr *pudp, ucb * thisdir)
{
//  static int tot;
  double delta_t = elapsed (thisdir->last_pkt_time, current_time);
  if (thisdir->last_ip_id == pip->ip_id &&
      thisdir->last_checksum == ntohs(pudp->uh_sum) && 
      delta_t < MIN_DELTA_T_UDP_DUP_PKT && thisdir->last_len == pip->ip_len)
    {
//       fprintf (fp_stdout, "dup udp %d , id = %u ",tot++, pip->ip_id);
//       fprintf (fp_stdout, "TTL: %d ID: %d Checksum: %d Delta_t: %g\n", 
//          pip->ip_ttl,pip->ip_id,ntohs(pudp->uh_sum),delta_t);
      thisdir->last_ip_id = pip->ip_id;
      thisdir->last_len = pip->ip_len;
      thisdir->last_checksum = ntohs(pudp->uh_sum); 
      return TRUE;
    }
//    fprintf (fp_stdout, "NOT dup udp %d\n",tot);
  thisdir->last_ip_id = pip->ip_id;
  thisdir->last_len = pip->ip_len;
  thisdir->last_checksum = ntohs(pudp->uh_sum); 
  return FALSE;
}
#endif

static udp_pair *
NewUTP (struct ip *pip, struct udphdr *pudp)
{
  udp_pair *pup;
  int old_new_udp_pairs = num_udp_pairs;
  int steps = 0;

  /* look for the next eventually available free block */
  num_udp_pairs++;
  num_udp_pairs = num_udp_pairs % MAX_UDP_PAIRS;
  /* make a new one, if possible */
  while ((num_udp_pairs != old_new_udp_pairs) && (utp[num_udp_pairs] != NULL)
	 && (steps < LIST_SEARCH_DEPT))
    {
      steps++;
      /* look for the next one */
//         fprintf (fp_stdout, "%d %d\n", num_udp_pairs, old_new_udp_pairs);
      num_udp_pairs++;
      num_udp_pairs = num_udp_pairs % MAX_UDP_PAIRS;
    }
  if (utp[num_udp_pairs] != NULL)
    {
      if (warn_MAX_)
	{
	  fprintf (fp_stderr, 
        "\nooopsss: number of simultaneous connection opened is greater then the maximum supported number!\n"
	    "you have to rebuild the source with a larger LIST_SEARCH_DEPT defined!\n"
	    "or possibly with a larger 'MAX_UDP_PAIRS' defined!\n");
	}
      warn_MAX_ = FALSE;
      return (NULL);
    }

  /* create a new UDP pair record and remember where you put it */
  pup = utp[num_udp_pairs] = utp_alloc ();

  /* grab the address from this packet */
  CopyAddr (&pup->addr_pair,
	    pip, ntohs (pudp->uh_sport), ntohs (pudp->uh_dport));

  pup->c2s.first_pkt_time.tv_sec = 0;
  pup->s2c.first_pkt_time.tv_sec = 0;

  pup->c2s.last_pkt_time.tv_sec = -1;
  pup->s2c.last_pkt_time.tv_sec = -1;

  pup->c2s.pup = pup;
  pup->s2c.pup = pup;

  pup->internal_src = internal_src;
  pup->internal_dst = internal_dst;

  pup->cloud_src = cloud_src;
  pup->cloud_dst = cloud_dst;

  if (crypto_src)
   {
     store_crypto_ip(&(pup->addr_pair.a_address.un.ip4));
   }

  if (crypto_dst)
   {
     store_crypto_ip(&(pup->addr_pair.b_address.un.ip4));
   }

  pup->crypto_src = crypto_src;
  pup->crypto_dst = crypto_dst;

  pup->c2s.type = UDP_UNKNOWN;
  pup->s2c.type = UDP_UNKNOWN;
  
  pup->c2s.kad_state = OUDP_UNKNOWN;
  pup->s2c.kad_state = OUDP_UNKNOWN;

  pup->c2s.uTP_state = UTP_UNKNOWN;
  pup->s2c.uTP_state = UTP_UNKNOWN;

  return (utp[num_udp_pairs]);
}


udp_pair *pup_hashtable[HASH_TABLE_SIZE] = { NULL };


/* connection records are stored in a hash table.  Buckets are linked	*/
/* lists sorted by most recent access.					*/
// static 
udp_pair *
FindUTP (struct ip * pip, struct udphdr * pudp, int *pdir)
{
  udp_pair **ppup_head = NULL;
  udp_pair *pup;
  udp_pair *pup_last;
  udp_pair tp_in;

  int prof_curr_clk;
  struct timeval prof_tm;
  double prof_curr_tm;
  struct tms prof_curr_tms;
  double cpu_sys,cpu_usr;


  int dir;
  hash hval;

  /* grab the address from this packet */
  CopyAddr (&tp_in.addr_pair, pip,
	    ntohs (pudp->uh_sport), ntohs (pudp->uh_dport));

  /* grab the hash value (already computed by CopyAddr) */
  hval = tp_in.addr_pair.hash % HASH_TABLE_SIZE;


  pup_last = NULL;
  ppup_head = &pup_hashtable[hval];
  for (pup = *ppup_head; pup; pup = pup->next)
    {
      ++search_count;
      if (SameConn (&tp_in.addr_pair, &pup->addr_pair, &dir))
	{
	  /* move to head of access list (unless already there) */
	  if (pup != *ppup_head)
	    {
	      pup_last->next = pup->next;	/* unlink */
	      pup->next = *ppup_head;	/* move to head */
	      *ppup_head = pup;
	    }
	  *pdir = dir;

/*
#ifdef RUNTIME_SKYPE_RESET
	  if (elapsed (pup->first_time, current_time) >
	      SKYPE_UPDATE_DELTA_TIME)
	    {
//            close_udp_flow (pup, -1, dir)
	      memset (&(pup->c2s.skype), 0, sizeof ((pup->c2s.skype)));
	      memset (&(pup->s2c.skype), 0, sizeof ((pup->s2c.skype)));
	      bayes_reset ((pup->c2s.bc_pktsize), BAYES_RESET_ZERO);
	      bayes_reset ((pup->c2s.bc_avgipg), BAYES_RESET_ZERO);

	    }
	  else
#endif
*/
	    return (pup);
	}
      pup_last = pup;
    }

    /* profile CPU */
    if (profile_cpu -> flag == HISTO_ON) {
        prof_curr_clk = (int)clock();
        gettimeofday(&prof_tm, NULL);
        prof_curr_tm = time2double(prof_tm)/1e6;
        times(&prof_curr_tms);
        
        
        if (prof_curr_tm - prof_last_tm > PROFILE_IDLE) {
            /* system cpu */
            cpu_sys = 1.0 * (prof_curr_tms.tms_stime - prof_last_tms.tms_stime) / prof_cps /
                  (prof_curr_tm - prof_last_tm) * 100;
            AVE_new_step(prof_tm, &ave_win_sys_cpu, cpu_sys);
            // system + user cpu
            //usr_cpu = 1.0 * (prof_curr_clk - prof_last_clk) / CLOCKS_PER_SEC / 
            //      (prof_curr_tm - prof_last_tm) * 100;
            /* user cpu */
            cpu_usr = 1.0 * (prof_curr_tms.tms_utime - prof_last_tms.tms_utime) / prof_cps /
                  (prof_curr_tm - prof_last_tm) * 100;
            AVE_new_step(prof_tm, &ave_win_usr_cpu, cpu_usr);
        
            prof_last_tm = prof_curr_tm;
            prof_last_clk = prof_curr_clk; 
            prof_last_tms = prof_curr_tms;
            max_cpu = (max_cpu < (cpu_usr+cpu_sys)) ? cpu_usr+cpu_sys : max_cpu;
            //printf("cpu:%.2f max:%.2f\n", cpu, max_cpu);
        }
    }

/* if is elapsed an IDLE_TIME from the last cleaning flow operation I will start
a new one */

  // we fire it at DOUBLE rate, but actually clean only those > UDP_IDLE_TIME
#ifdef WIPE_UDP_SINGLETONS
  if (elapsed (last_cleaned, current_time) > GARBAGE_FIRE_TIME)
#else
  if (elapsed (last_cleaned, current_time) > GARBAGE_FIRE_TIME)
#endif
    {
      if (threaded)
	{
	  pthread_mutex_lock (&flow_close_cond_mutex);

#ifdef DEBUG_THREAD
	  fprintf (fp_stdout, "Signaling thread FLOW CLOSE\n");
#endif
	  pthread_cond_signal (&flow_close_cond);
	  pthread_mutex_unlock (&flow_close_cond_mutex);

	  pthread_mutex_lock (&flow_close_started_mutex);
	  pthread_mutex_unlock (&flow_close_started_mutex);
#ifdef DEBUG_THREAD
	  fprintf (fp_stdout, "\n\nlocked thread FLOW CLOSE\n");
#endif
	}
      else
	trace_done_periodic ();
      last_cleaned = current_time;
    }

  fcount++;
  f_UDP_count++;
  add_histo (L4_flow_number, L4_FLOW_UDP);

  if (threaded)
    {
#ifdef DEBUG_THREAD_UTP
      fprintf (fp_stdout, "\n\nFindUTP: Try to lock thread UTP\n");
#endif

      pthread_mutex_lock (&utp_lock_mutex);

#ifdef DEBUG_THREAD_UTP
      fprintf (fp_stdout, "\n\nFindUTP: Got lock thread UTP\n");
#endif
    }
  pup = NewUTP (pip, pudp);

  /* put at the head of the access list */
  if (pup)
    {
      if (profile_flows->flag == HISTO_ON)
        AVE_arrival(current_time, &active_flows_win_UDP);
      tot_conn_UDP++;
      pup->next = *ppup_head;
      *ppup_head = pup;
    }
  /* profile number of missed udp session */
  else if (profile_flows->flag == HISTO_ON)
        AVE_arrival(current_time, &missed_flows_win_UDP);

  *pdir = C2S;

  if (threaded)
    {
      pthread_mutex_unlock (&utp_lock_mutex);

#ifdef DEBUG_THREAD_UTP
      fprintf (fp_stdout, "\n\nFindUTP: Unlocked thread TTP\n");
#endif
    }

  /*Return the new utp */

  return (pup);
}


void check_udp_obfuscate(ucb *thisdir, ucb *otherdir, u_short uh_ulen)
{
  if (thisdir->obfuscate_state==0 && otherdir->obfuscate_state==0)
   {
     switch(uh_ulen)
      {
	case 43:
	  thisdir->kad_state=OUDP_REQ43;
          break;
	case 59:
	  thisdir->kad_state=OUDP_REQ59;
          break;
        case 22:
          if (otherdir->obfuscate_last_len>=36 &&
              otherdir->obfuscate_last_len<=70)
            {
	      otherdir->kad_state=OUDP_SIZEX_22;
	      otherdir->obfuscate_state=1;
	      thisdir->pup->kad_state = OUDP_SIZEX_22;
            } 
          break;
	default:
	  if ( uh_ulen>=52 && (uh_ulen-52)%25 == 0)
	   {
	     if (otherdir->kad_state==OUDP_REQ43)
              {
		otherdir->kad_state=OUDP_RES52_K25;
		otherdir->obfuscate_state=1;
		thisdir->pup->kad_state=OUDP_RES52_K25;
	      }
             else if (uh_ulen==52 && 
        	      otherdir->obfuscate_last_len>=46 &&
        	      otherdir->obfuscate_last_len<=57)
              {
		otherdir->kad_state=OUDP_SIZEX_52;
		otherdir->obfuscate_state=1;
	        thisdir->pup->kad_state = OUDP_SIZEX_52;
              } 
             else
		otherdir->kad_state=OUDP_UNKNOWN;
             break;
           }
	  else if ( uh_ulen>=68 && (uh_ulen-68)%25 == 0)
	   {
	     if (otherdir->kad_state==OUDP_REQ59)
              {
		otherdir->kad_state=OUDP_RES68_K25;
		otherdir->obfuscate_state=1;
		thisdir->pup->kad_state=OUDP_RES68_K25;
	      }
             else
		otherdir->kad_state=OUDP_UNKNOWN;
             break;
           }
	  else if ( uh_ulen>=46 && uh_ulen<=57 &&
		    otherdir->obfuscate_last_len >=46 &&
        	    otherdir->obfuscate_last_len <=57)
           {
	     otherdir->kad_state=OUDP_SIZE_IN_46_57;
	     otherdir->obfuscate_state=1;
	     thisdir->pup->kad_state=OUDP_SIZE_IN_46_57;
	   }
          else
           {
	     thisdir->kad_state=OUDP_UNKNOWN;
	     otherdir->kad_state=OUDP_UNKNOWN;
           }
	  break;
      }

     thisdir->obfuscate_last_len = uh_ulen;

   }

  return;
}

void check_uTP(struct ip * pip, struct udphdr * pudp, void *plast,
                ucb *thisdir, ucb *otherdir)
{
  int payload_len;
  int data_len;
  unsigned char *base;
  tt_uint16 connection_id,seq_nr;

  if (thisdir->is_uTP==1 && otherdir->is_uTP==1)
    return;  /* Flow already classified */

  payload_len = ntohs (pudp->uh_ulen);
  /* This is the UDP complete length, included the header size */

  base = (unsigned char *) pudp;
  data_len = (unsigned char *) plast - (unsigned char *) base + 1;

  if (data_len < 28 || payload_len == 0)
    return;  /* Minimum uTP size is 8+20 bytes */
  
  if ( !( 
  	  ((base[8] & 0x31) || (base[8]==0x41) ) &&
     	  ( base[9]==0 || base[9]==1 || base[9]==2 )
     	)
     )  
    return; /* Minimal protocol matching failed*/

  switch(thisdir->uTP_state)
   {
/*
  Unknown --0x41-> SYN_SEEN --0x21-> completed open
  Unknown --0x41-> SYN_SEEN --0x11-> completed fin
  Unknown --0x41-> SYN_SEEN --0x31-> completed reset
  Unknown --0x01-> DATA_SEEN --0x21-> completed data_ack
  Unknown --0x01-> DATA_SEEN --0x01-> completed data_data
  Unknown --0x01-> DATA_SEEN --0x11-> completed data_fin
  Unknown --0x01-> DATA_SEEN --0x31-> completed data_reset
  Unknown --0x21-> ACK_SEEN --0x01-> completed ack_data
  Unknown --0x21-> ACK_SEEN --0x11-> completed ack_fin
  Unknown --0x21-> ACK_SEEN --0x31-> completed ack_reset
*/
     case UTP_UNKNOWN:
     case UTP_DATA_SENT:
     case UTP_SYN_SENT:
     case UTP_ACK_SENT:
       switch (base[8])
        {
	  case 0x01:
	    thisdir->uTP_conn_id=ntohs(*(tt_uint16 *)(base+10));
	    thisdir->uTP_state=UTP_DATA_SENT;
	    otherdir->uTP_state=UTP_DATA_SEEN;
	    break;
	  case 0x21:
	    thisdir->uTP_conn_id=ntohs(*(tt_uint16 *)(base+10));
	    thisdir->uTP_state=UTP_ACK_SENT;
	    otherdir->uTP_state=UTP_ACK_SEEN;
	    break;
	  case 0x41:
	    thisdir->uTP_conn_id=ntohs(*(tt_uint16 *)(base+10));
	    thisdir->uTP_syn_seq_nr=ntohs(*(tt_uint16 *)(base+24));
	    thisdir->uTP_state=UTP_SYN_SENT;
	    otherdir->uTP_state=UTP_SYN_SEEN;
	    break;
	  default:
	    break;
	}
       break;
     case UTP_SYN_SEEN:
       switch (base[8])
        {
	  case 0x11: /* SYN->FIN  check only the ID */ 
	    connection_id=ntohs(*(tt_uint16 *)(base+10));
	    if (connection_id==otherdir->uTP_conn_id)
	     {
	       thisdir->uTP_conn_id=connection_id;
	       otherdir->is_uTP=1;
	       thisdir->is_uTP=1;
	     }
	    break;
	  case 0x21:
	    seq_nr=ntohs(*(tt_uint16 *)(base+26));
	    connection_id=ntohs(*(tt_uint16 *)(base+10));
	    if ( seq_nr==otherdir->uTP_syn_seq_nr &&
	         connection_id==otherdir->uTP_conn_id)
	     {
	       thisdir->uTP_conn_id=connection_id;
	       otherdir->is_uTP=1;
	       thisdir->is_uTP=1;
	     }
	    break;
	  case 0x31: /* SYN->RESET  check only the ID */ 
	    connection_id=ntohs(*(tt_uint16 *)(base+10));
	    if (connection_id==otherdir->uTP_conn_id)
	     {
	       thisdir->uTP_conn_id=connection_id;
	       otherdir->is_uTP=1;
	       thisdir->is_uTP=1;
	     }
	    break;
	  case 0x41:
	    thisdir->uTP_conn_id=ntohs(*(tt_uint16 *)(base+10));
	    thisdir->uTP_syn_seq_nr=ntohs(*(tt_uint16 *)(base+24));
	    thisdir->uTP_state=UTP_SYN_SENT;
	    otherdir->uTP_state=UTP_SYN_SEEN;
	    break;
	  default:
	    break;
	}
       break;
     case UTP_DATA_SEEN:
       switch (base[8])
        {
	  case 0x01: /* DATA->DATA */
	  case 0x11: /* DATA->FIN */
	  case 0x21: /* DATA->ACK */
	    connection_id=ntohs(*(tt_uint16 *)(base+10));
	    if (connection_id==otherdir->uTP_conn_id+1 || 
	        connection_id==otherdir->uTP_conn_id-1)
	     {
	       thisdir->uTP_conn_id=connection_id;
	       otherdir->is_uTP=1;
	       thisdir->is_uTP=1;
	     }
	    break;
	  case 0x31: /* DATA->RESET */
	    connection_id=ntohs(*(tt_uint16 *)(base+10));
	    if (connection_id==otherdir->uTP_conn_id)
	     {
	       thisdir->uTP_conn_id=connection_id;
	       otherdir->is_uTP=1;
	       thisdir->is_uTP=1;
	     }
	    break;
	  default:
	    break;
	}
       break;
     case UTP_ACK_SEEN:
       switch (base[8])
        {
	  case 0x01: /* ACK->DATA */
	  case 0x11: /* ACK->FIN */
	  case 0x21: /* ACK->ACK */
	    connection_id=ntohs(*(tt_uint16 *)(base+10));
	    if (connection_id==otherdir->uTP_conn_id+1 || 
	        connection_id==otherdir->uTP_conn_id-1)
	     {
	       thisdir->uTP_conn_id=connection_id;
	       otherdir->is_uTP=1;
	       thisdir->is_uTP=1;
	     }
	    break;
	  case 0x31: /* ACK->RESET */
	    connection_id=ntohs(*(tt_uint16 *)(base+10));
	    if (connection_id==otherdir->uTP_conn_id)
	     {
	       thisdir->uTP_conn_id=connection_id;
	       otherdir->is_uTP=1;
	       thisdir->is_uTP=1;
	     }
	    break;
	  default:
	    break;
	}
       break;
     default:
       break;
   }

  if (thisdir->is_uTP==1 && otherdir->is_uTP==1)
   {
     if (thisdir->type==UDP_UNKNOWN ||
    	 thisdir->type==FIRST_RTP || 
    	 thisdir->type==FIRST_RTCP)
      { thisdir->type=P2P_UTP; }
     else if (thisdir->type==P2P_BT)
      { thisdir->type=P2P_UTPBT; }
     else
      { 
        // fprintf(fp_stderr, "uTP type overriding %d\n",thisdir->type);
    	thisdir->type=P2P_UTP; 
      }

     if (otherdir->type==UDP_UNKNOWN ||
    	 otherdir->type==FIRST_RTP || 
    	 otherdir->type==FIRST_RTCP)
      { otherdir->type=P2P_UTP; }
     else if (otherdir->type==P2P_BT)
      { otherdir->type=P2P_UTPBT; }
     else
      { 
        // fprintf(fp_stderr, "uTP type overriding %d\n",otherdir->type);
    	otherdir->type=P2P_UTP; 
      }
   }
  return;
}

void check_udp_vod(struct ip * pip, struct udphdr * pudp, void *plast,
                ucb *thisdir, ucb *otherdir)
{
  int payload_len;
  int data_len;
  unsigned char *base;

  if (thisdir->is_VOD==1 && otherdir->is_VOD==1)
    return;  /* Flow already classified */

  payload_len = ntohs (pudp->uh_ulen);
  /* This is the UDP complete length, included the header size */

  base = (unsigned char *) pudp;
  data_len = (unsigned char *) plast - (unsigned char *) base + 1;

  if (data_len < 9 || payload_len == 0)
   {
     thisdir->first_VOD=TRUE;
     return;  /* Minimum VOD size is 8+188 bytes */
   }
  
  /* According to the MPEG2 over IP information, we should always have
    7 PES (i.e. 188*7 bytes) */

  if (payload_len!=1324)
   {
     thisdir->first_VOD=TRUE;
     return;  /* Minimum VOD size is 8+188 bytes */
   }
    
  /* Check if we have at least two PES */
  
  if (data_len>196)
   {
     if (base[8]==0x47 && base[196]==0x47)
      {
        thisdir->is_VOD=1;
        otherdir->is_VOD=1;
        thisdir->first_VOD=FALSE;
        thisdir->type=UDP_VOD;
        otherdir->type=UDP_VOD;
        return;
      }
   }
  else  /* Only one PES, check the first byte (and the size 1324) */
   {
     if (base[8]==0x47)
      {
        thisdir->is_VOD=1;
        otherdir->is_VOD=1;
        thisdir->first_VOD=FALSE;
        thisdir->type=UDP_VOD;
        otherdir->type=UDP_VOD;
        return;
      }
   }
  
  /* Scrambled PES - Signature byte differs among flows, but is the same
     for all the frames in each flow. Match at least 3 packets */

  if (thisdir->first_VOD==TRUE)
   {
     thisdir->first_VOD=FALSE;
     thisdir->VOD_scrambled_sig[0]=base[8];
     if (data_len>196) 
       { thisdir->VOD_scrambled_sig[1]=base[196];}
     else
       { thisdir->VOD_scrambled_sig[1]=-1;}
     thisdir->VOD_count=1;
   }  
  else
   {
     if (data_len>196)
      {
        if (thisdir->VOD_scrambled_sig[0]==base[8] &&
            thisdir->VOD_scrambled_sig[1]==base[196] )
         {
            thisdir->VOD_count++;
        
   	    if (thisdir->VOD_count==3)
	     {
      	       thisdir->is_VOD=1;
      	       otherdir->is_VOD=1;
      	       thisdir->type=UDP_VOD;
      	       otherdir->type=UDP_VOD;
      	       return;
	     }
      	  }
     	 else
     	  {
     	    thisdir->first_VOD=TRUE;
     	  }
      }
     else
      {
        if (thisdir->VOD_scrambled_sig[0]==base[8] )
         {
            thisdir->VOD_count++;
        
   	    if (thisdir->VOD_count==3)
	     {
      	       thisdir->is_VOD=1;
      	       otherdir->is_VOD=1;
      	       thisdir->type=UDP_VOD;
      	       otherdir->type=UDP_VOD;
      	       return;
	     }
      	  }
     	 else
     	  {
     	    thisdir->first_VOD=TRUE;
     	  }
      }
   }  
  return;
}

void
udp_header_stat (struct udphdr * pudp, struct ip * pip)
{
  if (internal_src && !internal_dst)
    {
      L4_bitrate.out[UDP_TYPE] += ntohs (pip->ip_len);
      add_histo (udp_port_dst_out, (float) ntohs(pudp->uh_dport));
      if (cloud_dst)
       {
         L4_bitrate.c_out[UDP_TYPE] += ntohs (pip->ip_len);
       }
      else
       {
         L4_bitrate.nc_out[UDP_TYPE] += ntohs (pip->ip_len);
       }
    }
  else if (!internal_src && internal_dst)
    {
      L4_bitrate.in[UDP_TYPE] += ntohs (pip->ip_len);
      add_histo (udp_port_dst_in, (float) ntohs(pudp->uh_dport));
      if (cloud_src)
       {
         L4_bitrate.c_in[UDP_TYPE] += ntohs (pip->ip_len);
       }
      else
       {
         L4_bitrate.nc_in[UDP_TYPE] += ntohs (pip->ip_len);
       }
    }
#ifndef LOG_UNKNOWN
  else if (internal_src && internal_dst)
#else
  else
#endif
    {
      L4_bitrate.loc[UDP_TYPE] += ntohs (pip->ip_len);
      add_histo (udp_port_dst_loc, (float) ntohs(pudp->uh_dport));
    }

  return;
}

int
udp_flow_stat (struct ip * pip, struct udphdr * pudp, void *plast)
{

  udp_pair *pup_save;
  ucb *thisdir;
  ucb *otherdir;
  udp_pair tp_in;
  int dir;
  u_short uh_sport;		/* source port */
  u_short uh_dport;		/* destination port */
  u_short uh_ulen;		/* data length */

  /* make sure we have enough of the packet */
  if ((unsigned long) pudp + sizeof (struct udphdr) - 1 >
      (unsigned long) plast)
    {
      if (warn_printtrunc)
	fprintf (fp_stderr,
		 "UDP packet %lu truncated too short to trace, ignored\n",
		 pnum);
      ++ctrunc;
      return (FLOW_STAT_SHORT);
    }


  /* convert interesting fields to local byte order */
  uh_sport = ntohs (pudp->uh_sport);
  uh_dport = ntohs (pudp->uh_dport);
  uh_ulen = ntohs (pudp->uh_ulen);

  /* stop at this level of analysis */
  ++udp_trace_count;

  /* make sure this is one of the connections we want */
  pup_save = FindUTP (pip, pudp, &dir);

  ++packet_count;

  if (pup_save == NULL)
    {
      return (FLOW_STAT_NULL);
    }

  /* do time stats */
  if (ZERO_TIME (&pup_save->first_time))
    {
      pup_save->first_time = current_time;

    }
  pup_save->last_time = current_time;

  /* grab the address from this packet */
  CopyAddr (&tp_in.addr_pair, pip, uh_sport, uh_dport);

  /* figure out which direction this packet is going */
  if (dir == C2S)
    {
      thisdir = &pup_save->c2s;
      otherdir = &pup_save->s2c;
    }
  else
    {
      thisdir = &pup_save->s2c;
      otherdir = &pup_save->c2s;
    }

#ifdef CHECK_UDP_DUP
  /* check if this is a dupe udp */
  if (dup_udp_check (pip, pudp,thisdir)) {
    return (FLOW_STAT_DUP);
  }
#endif

  if ((thisdir->last_pkt_time.tv_sec) == -1)	/* is the first time I see this flow */
    {
      /* destination port of the flow */
      add_histo (udp_port_flow_dst, (float) (ntohs (pudp->uh_dport)));
      /* flow starting time */
      thisdir->first_pkt_time = current_time;
    }
  thisdir->last_pkt_time = current_time;

  /* do data stats */
  thisdir->data_bytes += uh_ulen - 8;	/* remove the UDP header */


  /* total packets stats */
  ++pup_save->packets;
  ++thisdir->packets;

   /*TOPIX*/
    /*TTL stats */
    if ((thisdir->ttl_min == 0) || (thisdir->ttl_min > (int) pip->ip_ttl))
    thisdir->ttl_min = (int) pip->ip_ttl;
  if (thisdir->ttl_max < (int) pip->ip_ttl)
    thisdir->ttl_max = (int) pip->ip_ttl;
  thisdir->ttl_tot += (u_llong) pip->ip_ttl;
   /*TOPIX*/
    //
    // NOW, this should be called by proto_analyzer...
    //
    //   p_rtp = getrtp (pudp, plast);
    //   if ((p_rtp) != NULL)
    //       rtpdotrace (thisdir, p_rtp, dir, pip);
    // 
    // 

    //fprintf(stderr, "BEFORE: %f\n", time2double(thisdir->skype->win.start));
    proto_analyzer (pip, pudp, PROTOCOL_UDP, thisdir, dir, plast);
    //fprintf(stderr, "AFTER: %f\n\n", time2double(thisdir->skype->win.start));

    //if (thisdir != NULL && thisdir->pup != NULL)
    make_udpL7_rate_stats(thisdir, ntohs(pip->ip_len));

  return (FLOW_STAT_OK);
}

void
behavioral_flow_wrap (struct ip *pip, void *pproto, int tproto, void *pdir,
	       int dir, void *hdr, void *plast)
{
  if (tproto == PROTOCOL_UDP)
    {
		ucb *thisdir;
		ucb *otherdir;
		udphdr *pudp = (udphdr *)hdr;
		udp_pair *pup_save = ((ucb *)pdir)->pup;
		u_short uh_ulen = ntohs (pudp->uh_ulen);		/* data length */
		
		/* figure out which direction this packet is going */
		if (dir == C2S)
		 {
		   thisdir = &pup_save->c2s;
		   otherdir = &pup_save->s2c;
		 }
		else
		 {
		   thisdir = &pup_save->s2c;
		   otherdir = &pup_save->c2s;
		 }
		
		if (pup_save->packets<MAX_UDP_UTP)
		  check_uTP(pip, pudp,plast,thisdir,otherdir);
		
		if (pup_save->packets<MAX_UDP_VOD)
		  check_udp_vod(pip, pudp,plast,thisdir,otherdir);
		
		if (pup_save->packets<MAX_UDP_OBFUSCATE)
		  check_udp_obfuscate(thisdir,otherdir,uh_ulen);
    }
  else
    {
		tcb *thisdir;
		tcphdr *ptcp = (tcphdr *) hdr;
		tcp_pair *ptp_save = ((tcb *) pdir)->ptp;
		
		if (ptp_save == NULL)
		  return;
		
		/* figure out which direction this packet is going */
		if (dir == C2S)
 		 {
		   thisdir = &ptp_save->c2s;
		 }
		else
		 {
		   thisdir = &ptp_save->s2c;
		 }
		
		/* Message size evaluation used for MSE detection might be incomplete 
		 if the last FIN segment is not considered */
		if (FIN_SET(ptcp) && thisdir != NULL && thisdir->ptp != NULL && 
		    thisdir->ptp->con_type == UNKNOWN_PROTOCOL)
		   mse_protocol_check(thisdir->ptp);
		
	 }

}


void
udptrace_init (void)
{
  static Bool initted = FALSE;

  if (initted)
    return;

  initted = TRUE;

  /* create an array to hold any pairs that we might create */
  utp = (udp_pair **) MallocZ (MAX_UDP_PAIRS * sizeof (udp_pair *));
}

void
udptrace_done (void)
{
    udp_pair *pup;
    int ix;
    int dir = -1;

    for (ix = 0; ix < MAX_UDP_PAIRS; ix++) {
        pup = utp[ix];
        // check if the flow has been already closed
        if (pup == NULL)
            continue;
        /* consider this udp connection */
            close_udp_flow(pup, ix, dir);
/*
        if (!con_cat) {
            //flush histos and call the garbage colletor
            //Note: close_udp_flow() calls make_udp_conn_stats()
            close_udp_flow(pup, ix, dir);
        }
        else
            //only flush histos
            make_udp_conn_stats (pup, TRUE);
*/
    }
}

void
make_udp_conn_stats (udp_pair * pup_save, Bool complete)
{
  double etime;

  if (complete)
    {
      if (pup_save->internal_src && !pup_save->internal_dst)
	{
	  add_histo (udp_cl_b_s_out, pup_save->c2s.data_bytes);
	  add_histo (udp_cl_b_s_in, pup_save->s2c.data_bytes);
	  add_histo (udp_cl_b_l_out, pup_save->c2s.data_bytes);
	  add_histo (udp_cl_b_l_in, pup_save->s2c.data_bytes);

	  add_histo (udp_cl_p_out, pup_save->c2s.packets);
	  add_histo (udp_cl_p_in, pup_save->s2c.packets);
	}
      else if (!pup_save->internal_src && pup_save->internal_dst)
	{
	  add_histo (udp_cl_b_s_out, pup_save->s2c.data_bytes);
	  add_histo (udp_cl_b_s_in, pup_save->c2s.data_bytes);
	  add_histo (udp_cl_b_l_out, pup_save->s2c.data_bytes);
	  add_histo (udp_cl_b_l_in, pup_save->c2s.data_bytes);

	  add_histo (udp_cl_p_out, pup_save->s2c.packets);
	  add_histo (udp_cl_p_in, pup_save->c2s.packets);
	}
      else if (pup_save->internal_src && pup_save->internal_dst)
	{
	  add_histo (udp_cl_b_s_loc, pup_save->s2c.data_bytes);
	  add_histo (udp_cl_b_s_loc, pup_save->c2s.data_bytes);
	  add_histo (udp_cl_b_l_loc, pup_save->s2c.data_bytes);
	  add_histo (udp_cl_b_l_loc, pup_save->c2s.data_bytes);

	  add_histo (udp_cl_p_loc, pup_save->s2c.packets);
	  add_histo (udp_cl_p_loc, pup_save->c2s.packets);

	}
      else
	{
	  if (warn_IN_OUT)
	    {
	      fprintf (fp_stderr, 
            "\nWARN: This udp flow is neither incoming nor outgoing: src - %s;",
		    HostName (pup_save->addr_pair.a_address));
	      fprintf (fp_stderr, " dst - %s!\n",
		      HostName (pup_save->addr_pair.b_address));
	      warn_IN_OUT = FALSE;
	    }
#ifndef LOG_UNKNOWN
	  return;
#else
/* fool the internal and external definition... */
	  add_histo (udp_cl_b_s_loc, pup_save->s2c.data_bytes);
	  add_histo (udp_cl_b_s_loc, pup_save->c2s.data_bytes);
	  add_histo (udp_cl_b_l_loc, pup_save->s2c.data_bytes);
	  add_histo (udp_cl_b_l_loc, pup_save->c2s.data_bytes);

	  add_histo (udp_cl_p_loc, pup_save->s2c.packets);
	  add_histo (udp_cl_p_loc, pup_save->c2s.packets);
#endif
	}
    }

  /* Statistics using plugins */

  make_proto_stat (pup_save, PROTOCOL_UDP);

  /* connection time */
  /* from microseconds to ms */
  etime = elapsed (pup_save->first_time, pup_save->last_time);
  etime = etime / 1000;
  add_histo (udp_tot_time, etime);
}



void
close_udp_flow (udp_pair * pup, int ix, int dir)
{

  extern udp_pair *pup_hashtable[];
  udp_pair **ppuph_head = NULL;
  udp_pair *puph_tmp, *puph, *puph_prev;
  unsigned int cleaned = 0;
  hash hval;
  int j;
  int tmp;

  if (threaded)
    {
#ifdef DEBUG_THREAD_UTP
      fprintf (fp_stdout, "\n\nTrace_done_periodic: trying lock thread UTP\n");
#endif
      pthread_mutex_lock (&utp_lock_mutex);
#ifdef DEBUG_THREAD_UTP
      fprintf (fp_stdout, "\n\nTrace_done_periodic: got lock thread UTP\n");
#endif
      if ((pup == NULL))
	/* someonelse already cleaned this pup */
	{
	  pthread_mutex_unlock (&utp_lock_mutex);
	  return;
	}
      if ((elapsed (pup->last_time, current_time) <= UDP_IDLE_TIME)
	  || (pup->last_time.tv_sec == 0 && pup->last_time.tv_usec == 0))
	{
	  /* someonelse already cleaned this pup */
	  pthread_mutex_unlock (&utp_lock_mutex);
	  return;
	}
    }
  /* must be cleaned */
  cleaned++;

  /* Consider this flow for statistic collections */
  make_udp_conn_stats (pup, TRUE);
  if (profile_flows->flag == HISTO_ON)
     AVE_departure(current_time, &active_flows_win_UDP);
  tot_conn_UDP--;

  /* free up hash element->.. */
  hval = pup->addr_pair.hash % HASH_TABLE_SIZE;

  ppuph_head = &pup_hashtable[hval];
  j = 0;
  puph_prev = *ppuph_head;
  for (puph = *ppuph_head; puph; puph = puph->next)
    {
      j++;
      if (SameConn (&pup->addr_pair, &puph->addr_pair, &tmp))
	{
	  puph_tmp = puph;
	  if (j == 1)
	    {
	      /* it is the top of the list */
	      pup_hashtable[hval] = puph->next;
	    }
	  else
	    {
	      /* it is in the middle of the list */
	      puph_prev->next = puph->next;
	    }
	  utp_release (puph_tmp);
	  break;
	}
      puph_prev = puph;
    }

  if (ix == -1)			/* I should look for the correct ix value */
    {
      for (ix = 0; ix < MAX_UDP_PAIRS; ++ix)
	{
	  //      pup = utp[ix];

	  if ((utp[ix] == NULL))
	    continue;

	  if (SameConn (&pup->addr_pair, &utp[ix]->addr_pair, &tmp))
	    {
	      break;
	    }
	}
    }

  utp[ix] = NULL;

  if (threaded)
    {
      pthread_mutex_unlock (&utp_lock_mutex);
#ifdef DEBUG_THREAD_UTP
      fprintf (fp_stdout, "\n\nTrace_done_periodic: released lock thread UTP\n");
#endif
    }

}
