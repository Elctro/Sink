﻿using System;
using System.Collections.Concurrent;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Security;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;

using Newtonsoft.Json;

namespace Kres.Man
{
    internal class CoreClient
    {
        private static readonly log4net.ILog log = log4net.LogManager.GetLogger(MethodBase.GetCurrentMethod().DeclaringType);
        public static Thread tCoreLoop;
        private static ConcurrentDictionary<Tuple<BigMath.Int128, BigMath.Int128>, Models.CacheIPRange> CacheRadius {get; set;} 

        private static void ThreadProc()
        {
            log.Info("Starting CoreClient thread.");

            while (true)
            {
                try
                {
                    if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
                    {
                        GetCoreCacheLinux();
                    }
                    else
                    {
                        GetCoreCache();
                    }

                    CacheLiveStorage.CoreCache.Updated = true;
                }
                catch (Exception ex)
                {
                    tCoreLoop = null;
                    log.Fatal($"{ex}");
                }

                var interval_seconds = Configuration.GetResolverUpdateInterval();
                Thread.Sleep(interval_seconds * 1000);
            }
        }

        private static bool ValidateRemoteCertificate(object sender, X509Certificate cert, X509Chain chain, SslPolicyErrors error)
        {
            //if (error == SslPolicyErrors.RemoteCertificateChainErrors)
            //{
            //    var cert2 = cert as X509Certificate2;
            //    if (cert2 != null)
            //    {
            //        if (string.Compare(cert2.Thumbprint, "E05B94180B1C02A89BAF451BF0F6A286AC529342", StringComparison.OrdinalIgnoreCase) == 0)
            //        {
            //            return true;
            //        }
            //    }
            //}

            return true;
        }

        private static void GetCoreCache()
        {
            log.Info("GetCoreCache()");

            string core_url = Configuration.GetCoreUrl();
            string certName = Configuration.GetPfxPath();
            string password = Configuration.GetPfxPassword();
            string resolver_id = Configuration.GetResolverId();
            
            X509Certificate2Collection certificates = new X509Certificate2Collection();
            certificates.Import(certName, password, X509KeyStorageFlags.MachineKeySet | X509KeyStorageFlags.PersistKeySet);

            ServicePointManager.ServerCertificateValidationCallback = (a, b, c, d) => true;
            HttpWebRequest req = (HttpWebRequest)WebRequest.Create(core_url);
            req.ServerCertificateValidationCallback += ValidateRemoteCertificate;
            req.AllowAutoRedirect = true;
            req.ClientCertificates = certificates;
            req.Method = "GET";
            req.ContentType = "application/x-protobuf";
            req.Headers["x-resolver-id"] = resolver_id;

            using (var response = req.GetResponseAsync().Result)
            {
                using (var stream = response.GetResponseStream())
                {
                    log.Debug($"Deserialize.");
                    var cache = ProtoBuf.Serializer.Deserialize<Models.Cache>(stream);
                    log.Debug($"Deserialized.");

                    if (cache.CustomLists != null)
                        log.Debug($"Custom List count = {cache.CustomLists.ToArray().Count()}");
                    if (cache.Domains != null)
                        log.Debug($"Domains count = {cache.Domains.ToArray().Count()}");
                    if (cache.IPRanges != null)
                        log.Debug($"IPRanges count = {cache.IPRanges.ToArray().Count()}");
                    if (cache.Policies != null)
                        log.Debug($"Policies count = {cache.Policies.ToArray().Count()}");

                    CacheLiveStorage.CoreCache = cache;
                    KresUpdater.UpdateNow();
                }
            }
        }

        public static void TestCoreCache(string path, string domain)
        {
            log.Info("GetCoreCacheManual()");

            using (var stream = new System.IO.FileStream(path, System.IO.FileMode.Open))
            {
                log.Debug($"Deserialize.");
                var cache = ProtoBuf.Serializer.Deserialize<Models.Cache>(stream);
                log.Debug($"Deserialized.");

                if (cache.CustomLists != null)
                    log.Debug($"Custom List count = {cache.CustomLists.ToArray().Count()}");
                if (cache.Domains != null)
                    log.Debug($"Domains count = {cache.Domains.ToArray().Count()}");
                if (cache.IPRanges != null)
                    log.Debug($"IPRanges count = {cache.IPRanges.ToArray().Count()}");
                if (cache.Policies != null)
                    log.Debug($"Policies count = {cache.Policies.ToArray().Count()}");

                var crc64 = Crc64.Compute(0, ASCIIEncoding.ASCII.GetBytes(domain));
                if (cache.Domains.Any(t => t.Crc64 == crc64))
                {
                    log.Debug("contains " + domain);
                }
            }
        }


        private static void GetCoreCacheLinux()
        {
            log.Info("GetCoreCacheLinux()");

            string core_url = Configuration.GetCoreUrl();
            string resolver_id = Configuration.GetResolverId();
            string certName = Configuration.GetPfxPath();
            string password = Configuration.GetPfxPassword();

            var clientHandler = new HttpClientHandler() { ClientCertificateOptions = ClientCertificateOption.Manual };
            clientHandler.ServerCertificateCustomValidationCallback = HttpClientHandler.DangerousAcceptAnyServerCertificateValidator;

            X509Certificate2 clientCertificate = new X509Certificate2(certName, password);
            clientHandler.ClientCertificates.Add(clientCertificate);
            var myClient = new HttpClient(clientHandler);
            myClient.DefaultRequestHeaders
              .Accept
              .Add(new System.Net.Http.Headers.MediaTypeWithQualityHeaderValue("application/x-protobuf"));

            var req = new HttpRequestMessage(HttpMethod.Get, core_url)
            {
                Version = new Version(2, 0)
            };
            req.Headers.Add("x-resolver-id", resolver_id);

            log.Info($"Request");
            using (var response = myClient.SendAsync(req).GetAwaiter().GetResult())
            {
                log.Info($"GetStream");
                using (var stream = response.Content.ReadAsStreamAsync().GetAwaiter().GetResult())
                {
                    log.Info($"Deserialize.");
                    var cache = ProtoBuf.Serializer.Deserialize<Models.Cache>(stream);

                    log.Info($"Deserialized.");
                    if (cache.CustomLists != null)
                        log.Info($"Custom List count = {cache.CustomLists.ToArray().Count()}");
                    if (cache.Domains != null)
                        log.Info($"Domains count = {cache.Domains.ToArray().Count()}");
                    if (cache.IPRanges != null)
                        log.Info($"IPRanges count = {cache.IPRanges.ToArray().Count()}");
                    if (cache.Policies != null)
                        log.Info($"Policies count = {cache.Policies.ToArray().Count()}");

                    if (cache.CustomLists == null &&
                        cache.Domains == null &&
                        cache.IPRanges == null &&
                        cache.Policies == null)
                    {
                        throw new Exception("Core has no cache!");
                    }

                    CacheLiveStorage.CoreCache = cache;
                }
            }
        }

        public static void Start()
        {
            tCoreLoop = new Thread(ThreadProc);
            tCoreLoop.Start();
        }

    }
}
