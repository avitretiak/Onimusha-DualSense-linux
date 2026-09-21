using Microsoft.Win32;
using System.Diagnostics;
using System.IO.Compression;
using System.Text.RegularExpressions;

namespace OnimushaDualSense;

static class Setup
{
    internal static IReadOnlyList<string> ManualGamePrompt { get; } =
    [
        "The game folder was not found automatically.",
        "Enter the path to the game folder that contains OnimushaWotS.exe.",
        "Do not enter the path to OnimushaWotS.exe itself.",
        @"Example: C:\Program Files (x86)\Steam\steamapps\common\OnimushaWotS"
    ];
    internal static readonly Dictionary<string, string> Resources = new()
    {
        ["natives/stm/gamedesign/system/adaptivetrigger/adaptivetriggersettingdata.user.3"] = "5a3299e925662dc5bb9c41905c82b90d50d0a340cef8eb8e5bc2129c3b52528b"
    };
    static Setup() { foreach (var item in SoundCatalog.Banks) Resources.Add(item.Key, item.Value); }
    static readonly Dictionary<string, (string Url, string Hash)> Tools = new()
    {
        ["pak"] = ("https://github.com/eigeen/ree-pak-rs/releases/download/v0.7.2/ree-pak-cli.exe", "bc4e561194a74faa3dbac06bd4ca0039eff85d047a8075c67895047212723101"),
        ["decoder"] = ("https://github.com/vgmstream/vgmstream/releases/download/r2117/vgmstream-win64.zip", "6c4a8a3813864fefed081bbd337dbc0ad93bf88e0b92f5db98d7ab258b22dc6c")
    };
    public static void Exec(string exe, params string[] args)
    {
        bool windowsTool = Path.GetExtension(exe).Equals(".exe", StringComparison.OrdinalIgnoreCase);
        var start = new ProcessStartInfo(exe) { UseShellExecute = false, CreateNoWindow = true };
        if (!OperatingSystem.IsWindows() && windowsTool)
        {
            string wine = Environment.GetEnvironmentVariable("WINE")?.Trim() ?? "wine";
            if (wine.Length == 0) wine = "wine";
            start = new ProcessStartInfo(wine) { UseShellExecute = false, CreateNoWindow = true };
            start.ArgumentList.Add(exe);
        }
        foreach (string arg in args) start.ArgumentList.Add(arg);
        try
        {
            using var process = Process.Start(start) ?? throw new IOException("Unable to start " + start.FileName);
            process.WaitForExit();
            if (process.ExitCode != 0)
                throw new IOException($"{Path.GetFileName(exe)} failed ({process.ExitCode}); verify the game path and tool arguments");
        }
        catch (System.ComponentModel.Win32Exception error) when (!OperatingSystem.IsWindows() && windowsTool)
        {
            throw new InvalidOperationException(
                $"Wine is required to run {Path.GetFileName(exe)} on Linux/Proton. Install Wine or set WINE to its executable path, then retry.",
                error);
        }
    }
    public static string? Discover()
    {
        if (!OperatingSystem.IsWindows()) return null;
        var libraries = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (hive, key, name) in new[] { (Registry.CurrentUser, @"Software\Valve\Steam", "SteamPath"), (Registry.LocalMachine, @"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath") })
        {
            using var handle = hive.OpenSubKey(key);
            if (handle?.GetValue(name) is string path) libraries.Add(path);
        }
        foreach (string root in libraries.ToArray())
        {
            string file = Path.Combine(root, "steamapps/libraryfolders.vdf");
            if (File.Exists(file)) foreach (Match match in Regex.Matches(File.ReadAllText(file), "\"path\"\\s+\"([^\"]+)\"")) libraries.Add(match.Groups[1].Value.Replace(@"\\", @"\"));
        }
        var found = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string library in libraries)
        {
            string file = Path.Combine(library, "steamapps/appmanifest_2638890.acf");
            if (!File.Exists(file)) continue;
            var match = Regex.Match(File.ReadAllText(file), "\"installdir\"\\s+\"([^\"]+)\"");
            if (!match.Success) continue;
            string game = Path.GetFullPath(Path.Combine(library, "steamapps/common", match.Groups[1].Value));
            if (File.Exists(Path.Combine(game, "OnimushaWotS.exe"))) found.Add(game);
        }
        return found.Count == 1 ? found.First() : null;
    }
    static string Download(string kind)
    {
        var tool = Tools[kind]; Directory.CreateDirectory(Files.Data("tools"));
        string path = Files.Data("tools/" + Path.GetFileName(new Uri(tool.Url).AbsolutePath));
        if (!File.Exists(path) || Files.Sha(path) != tool.Hash)
        {
            Console.WriteLine("Downloading official asset-extraction tool: " + tool.Url);
            using var client = new HttpClient { Timeout = TimeSpan.FromMinutes(3) };
            client.DefaultRequestHeaders.UserAgent.ParseAdd("Onimusha-DualSense-Asset-Generator/1.2.0");
            string temp = path + ".download";
            using (var response = client.GetAsync(tool.Url, HttpCompletionOption.ResponseHeadersRead).GetAwaiter().GetResult())
            {
                response.EnsureSuccessStatusCode();
                using var input = response.Content.ReadAsStream(); using var output = File.Create(temp); input.CopyTo(output);
            }
            if (Files.Sha(temp) != tool.Hash) { File.Delete(temp); throw new InvalidDataException("Download checksum mismatch; nothing was executed"); }
            File.Move(temp, path, true);
        }
        if (kind == "pak") return path;
        string folder = Files.Data("tools/vgmstream-r2117"); Directory.CreateDirectory(folder);
        ZipFile.ExtractToDirectory(path, folder, true); // BCL rejects directory traversal entries.
        string executable = Path.Combine(folder, "vgmstream-cli.exe");
        if (!File.Exists(executable))
            throw new InvalidDataException("The decoder archive did not contain vgmstream-cli.exe; remove the cached archive and retry.");
        return executable;
    }
    internal static Dictionary<string, byte[]> Chunks(byte[] data)
    {
        var result = new Dictionary<string, byte[]>(); int pos = 0;
        while (pos < data.Length)
        {
            if (pos + 8 > data.Length) throw new InvalidDataException("Truncated bank");
            string tag = System.Text.Encoding.ASCII.GetString(data, pos, 4); int length = checked((int)BitConverter.ToUInt32(data, pos + 4));
            pos += 8; if (length > data.Length - pos) throw new InvalidDataException("Truncated chunk");
            result[tag] = data[pos..(pos + length)]; pos += length;
        }
        return result;
    }
    internal static void PrepareTriggers(string extracted)
    {
        const string resource = "natives/stm/gamedesign/system/adaptivetrigger/adaptivetriggersettingdata.user.3";
        string path = Path.Combine(extracted, resource);
        if (Files.Sha(path) != Resources[resource]) throw new InvalidDataException("Unsupported adaptive trigger data");
        byte[] data = File.ReadAllBytes(path); var profiles = new List<object>();
        foreach (int offset in new[] { 0x90, 0xdc })
        {
            int kind = BitConverter.ToInt32(data, offset), which = BitConverter.ToInt32(data, offset + 4);
            if (BitConverter.ToUInt32(data, offset + 16) != 10 || which is < 0 or > 2) throw new InvalidDataException("Unsupported trigger profile");
            float[] powers = Enumerable.Range(0, 10).Select(i => BitConverter.ToSingle(data, offset + 20 + 4 * i)).ToArray(); Protocol.Feedback(powers);
            profiles.Add(new { _Type = kind, _Which = which, _Frequency = BitConverter.ToSingle(data, offset + 8), _IsMultiPosition = BitConverter.ToUInt32(data, offset + 12) != 0, _PowerList = powers, _Power = BitConverter.ToSingle(data, offset + 60), _StartPressPosition = BitConverter.ToSingle(data, offset + 64), _EndPressPosition = BitConverter.ToSingle(data, offset + 68), _EndPower = BitConverter.ToSingle(data, offset + 72) });
        }
        Files.Save(Files.Data("trigger_profiles.json"), new { source = "local game assets", profiles });
    }
    public static void PrepareAssets(string[] args)
    {
        string? Option(string key)
        {
            int index = Array.IndexOf(args, key);
            return index >= 0 && index + 1 < args.Length ? args[index + 1] : null;
        }

        string? game = Option("--game") ?? Discover();
        if (game == null)
        {
            foreach (string line in ManualGamePrompt) Console.WriteLine(line);
            Console.Write("Game folder path: ");
            game = Console.ReadLine()?.Trim().Trim('"');
        }
        game = Path.GetFullPath(game ?? throw new InvalidOperationException("A game folder is required to extract developer assets"));
        if (!File.Exists(Path.Combine(game, "OnimushaWotS.exe")))
            throw new InvalidDataException($"OnimushaWotS.exe was not found in '{game}'. Enter the game folder containing the executable, not the executable path.");

        string pak = Path.GetFullPath(Option("--pak-tool") ?? Download("pak"));
        string decoder = Path.GetFullPath(Option("--decoder") ?? Download("decoder"));
        string scratch = Files.Data("extract-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(scratch);
        try
        {
            string paths = Path.Combine(scratch, "paths.list"), extracted = Path.Combine(scratch, "extracted");
            File.WriteAllText(paths, string.Join('\n', Resources.Keys) + "\n");
            string[] packs = [Path.Combine(game, "re_chunk_000.pak"), .. Directory.GetFiles(game, "re_chunk_000.pak.patch_*.pak").Order(StringComparer.Ordinal)];
            foreach (string pack in packs) Exec(pak, "unpack", "-p", paths, "-i", pack, "-o", extracted, "--skip-unknown", "--override");
            PrepareTriggers(extracted);
            SoundHaptics.Prepare(extracted, decoder);
            DefenseSounds.Prepare(extracted, decoder);
        }
        finally { Directory.Delete(scratch, true); }

        PreparedWaves.Prepare();
        Console.WriteLine("Developer asset generation complete. Catalog, waves, trigger profiles, and haptic metadata are ready in the developer data directory.");
    }
}
