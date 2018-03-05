/* Convenience macro to declare module API. */
#define C_MOD_WHALEBONE "\x09""whalebone"

#include "lib/module.h"
#include <pthread.h>
#include <syslog.h>
#include <lib/rplan.h>

#include "whalebone.h"

static void* observe(void *arg)
{
  /* ... do some observing ... */
  openlog("whalebone",  LOG_CONS | LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Loading");
  closelog();

  unsigned long long ret = 0;
  if ((ret = loader_init()) != 0)
  {
  	openlog("whalebone",  LOG_CONS | LOG_PID, LOG_USER);
  	syslog(LOG_INFO, "CSV load failed");
  	closelog();
  	return (void *)-1;
  }

  pthread_t thr_id;
  if ((ret = pthread_create(&thr_id, NULL, &socket_server, NULL)) != 0) 
  {
  	openlog("whalebone",  LOG_CONS | LOG_PID, LOG_USER);
  	syslog(LOG_INFO, "Create thread failed");
  	closelog();
    return (void *)ret;  
  }

  openlog("whalebone",  LOG_CONS | LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Loaded");
  closelog();

  return NULL;
}

static int load(struct kr_module *module, const char *path)
{
    return kr_ok();
}

static int parse_addr_str(struct sockaddr_storage *sa, const char *addr) {
    int family = strchr(addr, ':') ? AF_INET6 : AF_INET;
    memset(sa, 0, sizeof(struct sockaddr_storage));
    sa->ss_family = family;
    char *addr_bytes = (char *)kr_inaddr((struct sockaddr *)sa);
    if (inet_pton(family, addr, addr_bytes) < 1) {
        return kr_error(EILSEQ);
    }
    return 0;
}

static int collect_rtt(kr_layer_t *ctx, knot_pkt_t *pkt)
{
    struct kr_request *req = ctx->req;
    struct kr_query *qry = req->current_query;
    if (qry->flags.CACHED || !req->qsource.addr) {
        return ctx->state;
    }
    
    const struct sockaddr *res = req->qsource.addr;
    char *s = NULL;
    switch(res->sa_family) {
        case AF_INET: {
            struct sockaddr_in *addr_in = (struct sockaddr_in *)res;
            s = malloc(INET_ADDRSTRLEN);
            inet_ntop(AF_INET, &(addr_in->sin_addr), s, INET_ADDRSTRLEN);
            break;
        }
        case AF_INET6: {
            struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)res;
            s = malloc(INET6_ADDRSTRLEN);
            inet_ntop(AF_INET6, &(addr_in6->sin6_addr), s, INET6_ADDRSTRLEN);
            break;
        }
        default:
        {
            logtosyslog("not valid addr");
            return ctx->state;
            break;
        }
    }
    char message[KNOT_DNAME_MAXLEN] = {};
    sprintf(message, "IP address: %s", s);
    logtosyslog(message); 
    free(s);

    return ctx->state;
}

static int redirect(struct kr_request * request, struct kr_query *last)
{
  uint16_t msgid = knot_wire_get_id(request->answer->wire);
  kr_pkt_recycle(request->answer);

  knot_pkt_put_question(request->answer, last->sname, last->sclass, last->stype);
  knot_pkt_begin(request->answer, KNOT_ANSWER); //AUTHORITY?

  struct sockaddr_storage sinkhole;
  const char *sinkit_sinkhole = "94.237.30.217";
  if (parse_addr_str(&sinkhole, sinkit_sinkhole) != 0) {
      return kr_error(EINVAL);
  }

  size_t addr_len = kr_inaddr_len((struct sockaddr *)&sinkhole);
  const uint8_t *raw_addr = (const uint8_t *)kr_inaddr((struct sockaddr *)&sinkhole);
  static knot_rdata_t rdata_arr[RDATA_ARR_MAX];

  knot_wire_set_id(request->answer->wire, msgid);

  kr_pkt_put(request->answer, last->sname, 120, KNOT_CLASS_IN, KNOT_RRTYPE_A, raw_addr, addr_len);

  return KNOT_STATE_DONE;
}

static int search(kr_layer_t *ctx, const char * querieddomain, struct ip_addr * origin, struct kr_request * request, struct kr_query * last)
{
  char message[KNOT_DNAME_MAXLEN] = {};
   
  unsigned long long crc = crc64(0, (const unsigned char*)querieddomain, strlen(querieddomain));
  domain domain_item = {};
  if (cache_domain_contains(cached_domain, crc, &domain_item))
  {
      //sprintf(message, "detected '%s'", querieddomain);
      //logtosyslog(message);
      
      iprange iprange_item = {};
      if (cache_iprange_contains(cached_iprange, origin, &iprange_item))
      {                                          
        //sprintf(message, "detected '%s' matches ip range with ident '%s' policy '%d'", querieddomain, iprange_item.identity, iprange_item.policy_id);
        //logtosyslog(message);
        
        if (strlen(iprange_item.identity) > 0)
        {
          if (cache_customlist_blacklist_contains(cached_customlist, iprange_item.identity, crc))
          {
            //sprintf(message, "identity '%s' got '%s' blacklisted.", iprange_item.identity, querieddomain);
            //logtosyslog(message);
            return redirect(request, last);                          
          }
          if (cache_customlist_whitelist_contains(cached_customlist, iprange_item.identity, crc))
          {
            //sprintf(message, "identity '%s' got '%s' whitelisted.", iprange_item.identity, querieddomain);
            //logtosyslog(message);
            return KNOT_STATE_DONE;
          }
        }
        //sprintf(message, "no identity match, checking policy..");
        //logtosyslog(message);
       
        policy policy_item = {}; 
        if (cache_policy_contains(cached_policy, iprange_item.policy_id, &policy_item))
        {  
          int domain_flags = cache_domain_get_flags(domain_item.flags, iprange_item.policy_id);
          if (domain_flags == 0)
          {
            //sprintf(message, "policy has strategy flags_none");
            //logtosyslog(message);
          }
          if (domain_flags & flags_accuracy) 
          {
            //sprintf(message, "policy '%d' strategy=>'accuracy' audit='%d' block='%d' '%s'='%d' accuracy", iprange_item.policy_id, policy_item.audit, policy_item.block, querieddomain, domain_item.accuracy);
            //logtosyslog(message);
            if (domain_item.accuracy >= policy_item.block)
            {
              return redirect(request, last);  
            } 
            else 
            if (domain_item.accuracy > policy_item.audit)
            {
              sprintf(message, "policy '%d' strategy=>'accuracy' audit='%d' block='%d' '%s'='%d' accuracy", iprange_item.policy_id, policy_item.audit, policy_item.block, querieddomain, domain_item.accuracy);
              logtoaudit(message);
            }
          }
          if (domain_flags & flags_blacklist) 
          {
            //sprintf(message, "policy '%d' strategy=>'blacklist' audit='%d' block='%d' '%s'='%d' accuracy", iprange_item.policy_id, policy_item.audit, policy_item.block, querieddomain, domain_item.accuracy);
            //logtosyslog(message);
            return redirect(request, last);                             
          }
          if (domain_flags & flags_whitelist) 
          {
            //sprintf(message, "policy '%d' strategy=>'whitelist' audit='%d' block='%d' '%s'='%d' accuracy", iprange_item.policy_id, policy_item.audit, policy_item.block, querieddomain, domain_item.accuracy);
            //logtosyslog(message);
          }
          if (domain_flags & flags_drop) 
          {
            //sprintf(message, "policy '%d' strategy=>'drop' audit='%d' block='%d' '%s'='%d' accuracy", iprange_item.policy_id, policy_item.audit, policy_item.block, querieddomain, domain_item.accuracy);
            //logtosyslog(message);
          }
        }
        else                      
        {
          int domain_flags = cache_domain_get_flags(domain_item.flags, 0);
          if (domain_flags & flags_accuracy)
          {
              //sprintf(message, "'%s' no-policy => domain-policy =>'accuracy'", querieddomain);
              //logtosyslog(message);                                                           
              sprintf(message, "auditing '%s' no-policy => domain-policy =>'accuracy'", querieddomain);
              logtoaudit(message);
          }
          if (domain_flags & flags_blacklist)
          {
              //sprintf(message, "'%s' no-policy => domain-policy =>'blacklist'", querieddomain);
              //logtosyslog(message);
              return redirect(request, last); 
          }
          if (domain_flags & flags_whitelist)
          {
              //sprintf(message, "'%s' no-policy => domain-policy =>'whitelist'", querieddomain);
              //logtosyslog(message);
              return KNOT_STATE_DONE;
          }
          if (domain_flags & flags_drop)
          {
              //sprintf(message, "'%s' no-policy => domain-policy =>'drop'", querieddomain);
              //logtosyslog(message);
              
              //TODO
          }     
        }
      }
      else
      {
        //sprintf(message, "no match to iprange");
        //logtosyslog(message);                    
      }
  }
  
  return KNOT_STATE_NOOP;
}

static int explode(kr_layer_t *ctx, char * domain, struct ip_addr * origin, struct kr_request * request, struct kr_query * last)
{
  char *ptr = domain;
  ptr += strlen(domain);
  int result = ctx->state;
  int found = 0;    
  while (ptr-- != (char *)domain)
  {
    if (ptr[0] == '.')
    {   
      if (++found > 1)
      {        
        if ((result = search(ctx, ptr + 1, origin, request, last)) == KNOT_STATE_DONE)
        {
          return result;
        }
      }
    }
    else if (ptr == (char *)&domain)
    {
      if ((result = search(ctx, ptr, origin, request, last)) == KNOT_STATE_DONE)
      {
        return result;
      }
    }
  }
  
  return ctx->state;
}

static int collect(kr_layer_t *ctx)
{
  struct kr_request *request = (struct kr_request *)ctx->req;
  struct kr_rplan *rplan = &request->rplan;

	if (!request->qsource.addr) {
		//sprintf(message, "request has no source address");
		//logtosyslog(message);

		return ctx->state;
	}

	const struct sockaddr *res = request->qsource.addr;
	//char *s = NULL;
  struct ip_addr origin = {};
	switch (res->sa_family) {
  	case AF_INET: 
    {
  		struct sockaddr_in *addr_in = (struct sockaddr_in *)res;
  		//s = malloc(INET_ADDRSTRLEN);
  		//inet_ntop(AF_INET, &(addr_in->sin_addr), s, INET_ADDRSTRLEN);
      origin.family = AF_INET;
      memcpy(&origin.ipv4_sin_addr, &(addr_in->sin_addr), 4);    
  		break;
  	}
  	case AF_INET6: 
    {
  		struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)res;
  		//s = malloc(INET6_ADDRSTRLEN);
  		//inet_ntop(AF_INET6, &(addr_in6->sin6_addr), s, INET6_ADDRSTRLEN);
      origin.family = AF_INET6;
      memcpy(&origin.ipv6_sin_addr, &(addr_in6->sin6_addr), 16); 
  		break;
  	}
  	default:
  	{
  		//sprintf(message, "qsource is invalid");
  		//logtosyslog(message);
  		return ctx->state;
  		break;
  	}
	}
	//sprintf(message, "[%s] request", s);
	//logtosyslog(message);
	//free(s);

    char qname_str[KNOT_DNAME_MAXLEN];
    if (rplan->resolved.len > 0)
    {
        bool sinkit = false;
        uint16_t rclass = 0;
        struct kr_query *last = array_tail(rplan->resolved);
        const knot_pktsection_t *ns = knot_pkt_section(request->answer, KNOT_ANSWER);

        if (ns == NULL)
        {
            logtosyslog("ns = NULL");
            return ctx->state;
        }

        for (unsigned i = 0; i < ns->count; ++i)
        {
            const knot_rrset_t *rr = knot_pkt_rr(ns, i);

            if (rr->type == KNOT_RRTYPE_A || rr->type == KNOT_RRTYPE_AAAA)
            {
              char querieddomain[KNOT_DNAME_MAXLEN];
              knot_dname_to_str(querieddomain, rr->owner, KNOT_DNAME_MAXLEN);
              
              int domainLen = strlen(querieddomain);
              if (querieddomain[domainLen - 1] == '.')
              {
                  querieddomain[domainLen - 1] = '\0';
              }
              
              return explode(ctx, (char *)&querieddomain, &origin, request, last); 
            }
        }
    }

    return ctx->state;
}

KR_EXPORT
const kr_layer_api_t *whalebone_layer(struct kr_module *module) {
        static kr_layer_api_t _layer = {
				//.consume = &collect_rtt,
                .finish = &collect,
        };
        /* Store module reference */
        _layer.data = module;
        return &_layer;
}

KR_EXPORT
int whalebone_init(struct kr_module *module)
{
        /* Create a thread and start it in the background. */
        pthread_t thr_id;
        int ret = pthread_create(&thr_id, NULL, &observe, NULL);
        if (ret != 0) {
                return kr_error(errno);
        }

        /* Keep it in the thread */
        module->data = (void *)thr_id;
        return kr_ok();
}

KR_EXPORT
int whalebone_deinit(struct kr_module *module)
{
        /* ... signalize cancellation ... */
        void *res = NULL;
        pthread_t thr_id = (pthread_t) module->data;
        int ret = pthread_join(thr_id, res);
        if (ret != 0) {
                return kr_error(errno);
        }

        return kr_ok();
}

KR_MODULE_EXPORT(whalebone)