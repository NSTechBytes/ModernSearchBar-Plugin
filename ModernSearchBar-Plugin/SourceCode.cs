using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Xml;
using Rainmeter;

namespace ModernSearchBar
{
    internal class Measure
    {
        private string chromeHistoryPath;
        private string copiedDbPath;
        private string sqliteExePath;
        private string type;
        private int limit;
        private API api;

        internal Measure() { }

        internal void Reload(API api, ref double maxValue)
        {
            this.api = api;

           
            type = api.ReadString("Type", "RecentHistory").Trim();
            limit = api.ReadInt("Limit", 6);

         
            chromeHistoryPath = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                @"Google\Chrome\User Data\Default\History"
            );

         
            copiedDbPath = Path.Combine(Path.GetTempPath(), "ChromeHistory_Copy");

            sqliteExePath = api.ReadString("SQLitePath", "").Trim();


        }

        internal double Update()
        {
            return 0.0;
        }

        internal string GetString()
        {
            if (type.Equals("RecentHistory", StringComparison.OrdinalIgnoreCase))
            {
                return GetRecentHistory();
            }
            else if (type.Equals("TopTrends", StringComparison.OrdinalIgnoreCase))
            {
                return GetTopTrends();
            }
            else
            {
                api.Log(
                    API.LogType.Error,
                    $"ModernSearchBar.dll: Invalid Type '{type}' specified."
                );
                return "Invalid Type specified.";
            }
        }

        private string GetRecentHistory()
        {
            if (!File.Exists(chromeHistoryPath))
            {
                api.Log(
                    API.LogType.Error,
                    $"ModernSearchBar.dll: Chrome history file not found at '{chromeHistoryPath}'."
                );
                return "Chrome history not found.";
            }

            if (string.IsNullOrEmpty(sqliteExePath) || !File.Exists(sqliteExePath))
            {
                api.Log(
                    API.LogType.Error,
                    "ModernSearchBar.dll: 'SQLitePath' is not specified or invalid."
                );
                return "SQlite.exe not found.";
            }

            try
            {
                File.Copy(chromeHistoryPath, copiedDbPath, true);
                string query =
                    $"SELECT title FROM urls GROUP BY title ORDER BY MAX(last_visit_time) DESC LIMIT {limit};";

                return ExecuteSQLiteQuery(query);
            }
            catch (Exception ex)
            {
                api.Log(
                    API.LogType.Error,
                    $"ModernSearchBar.dll: Error retrieving recent history: {ex.Message}"
                );

                return "Error retrieving history.";
            }
        }

        private string GetTopTrends()
        {
           
            if (!IsInternetAvailable())
            {
                return "Not Internet";
            }

            string url = "https://trends.google.com/trends/trendingsearches/daily/rss?geo=US";
            try
            {
                using (var client = new System.Net.WebClient())
                {
                    string rssData = client.DownloadString(url);

                  
                    var xmlDoc = new System.Xml.XmlDocument();
                    xmlDoc.LoadXml(rssData);

                    var items = xmlDoc.GetElementsByTagName("item");
                    var result = new List<string>();

                    for (int i = 0; i < Math.Min(items.Count, limit); i++)
                    {
                        var titleNode = items[i]["title"];
                        if (titleNode != null)
                        {
                            result.Add(titleNode.InnerText);
                        }
                    }

                    return string.Join("|", result);
                }
            }
            catch (Exception ex)
            {
                api.Log(
                    API.LogType.Error,
                    $"ModernSearchBar.dll: Error retrieving top trends: {ex.Message}"
                );

                return "Error retrieving top trends.";
            }
        }

       
        private bool IsInternetAvailable()
        {
            try
            {
                using (var client = new System.Net.WebClient())
                using (client.OpenRead("http://www.google.com"))
                {
                    return true;
                }
            }
            catch
            {
                return false;
            }
        }

        private string ExecuteSQLiteQuery(string query)
        {
            string output = string.Empty;

            if (!File.Exists(sqliteExePath))
            {
                api.Log(API.LogType.Error, "ModernSearchBar.dll: SQLite executable not found.");
                return "SQLite executable not found.";
            }

            string arguments = $"\"{copiedDbPath}\" \"{query}\"";

            try
            {
                ProcessStartInfo processStartInfo = new ProcessStartInfo
                {
                    FileName = sqliteExePath,
                    Arguments = arguments,
                    RedirectStandardOutput = true,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                };

                using (Process process = Process.Start(processStartInfo))
                {
                    output = process.StandardOutput.ReadToEnd();
                    process.WaitForExit();
                }
            }
            catch (Exception ex)
            {
                api.Log(
                    API.LogType.Error,
                    $"ModernSearchBar.dll: Error querying history database: {ex.Message}"
                );
                return "Error querying history.";
            }

            
            string[] lines = output.Split(
                new[] { '\r', '\n' },
                StringSplitOptions.RemoveEmptyEntries
            );

            return string.Join("|", lines);
        }
    }

    public static class Plugin
    {
        static IntPtr StringBuffer = IntPtr.Zero;

        [DllExport]
        public static void Initialize(ref IntPtr data, IntPtr rm)
        {
            data = GCHandle.ToIntPtr(GCHandle.Alloc(new Measure()));
        }

        [DllExport]
        public static void Finalize(IntPtr data)
        {
            GCHandle.FromIntPtr(data).Free();

            if (StringBuffer != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(StringBuffer);
                StringBuffer = IntPtr.Zero;
            }
        }

        [DllExport]
        public static void Reload(IntPtr data, IntPtr rm, ref double maxValue)
        {
            Measure measure = (Measure)GCHandle.FromIntPtr(data).Target;
            measure.Reload(new API(rm), ref maxValue);
        }

        [DllExport]
        public static double Update(IntPtr data)
        {
            Measure measure = (Measure)GCHandle.FromIntPtr(data).Target;
            return measure.Update();
        }

        [DllExport]
        public static IntPtr GetString(IntPtr data)
        {
            Measure measure = (Measure)GCHandle.FromIntPtr(data).Target;
            string value = measure.GetString();

            IntPtr stringBuffer = IntPtr.Zero;
            if (!string.IsNullOrEmpty(value))
            {
                stringBuffer = Marshal.StringToHGlobalUni(value);
            }

            return stringBuffer;
        }
    }
}
