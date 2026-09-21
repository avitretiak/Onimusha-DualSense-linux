namespace OnimushaDualSense;

static class Program
{
    static int Main(string[] args)
    {
        Console.OutputEncoding = System.Text.Encoding.UTF8;
        string command = args.FirstOrDefault() ?? "help";
        try
        {
            AppHost.Initialize();
            switch (command)
            {
                case "prepare-assets": Setup.PrepareAssets(args); return 0;
                case "prepare-waves": PreparedWaves.Prepare(args.Contains("--force")); return 0;
                case "diagnose":
                    Console.WriteLine($"Onimusha DualSense 1.2.0 / {System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription}");
                    Console.WriteLine("DualSense HID devices: " + Hid.Find().Count);
                    Console.WriteLine("Asset source discovery: " + Setup.Discover()); return 0;
#if DEVELOPER
                case "inspect": Inspector.Launch(); return 0;
                case "inspect-server": return Inspector.Run(!args.Contains("--no-open"));
                case "diagnose-audio": Audio.Diagnose(); return 0;
                case "test": return Tests.Run();
                case "audit-haptics": HapticAudit.Run(); return 0;
                case "verify-prepared": HapticAudit.VerifyPrepared(); return 0;
                case "prepare-sounds":
                    if (args.Length != 3) throw new ArgumentException("prepare-sounds <extracted-root> <decoder-exe>");
                    SoundHaptics.Prepare(Path.GetFullPath(args[1]), Path.GetFullPath(args[2])); return 0;
                case "prepare-defense":
                    if (args.Length != 3) throw new ArgumentException("prepare-defense <extracted-root> <decoder-exe>");
                    DefenseSounds.Prepare(Path.GetFullPath(args[1]), Path.GetFullPath(args[2])); return 0;
#endif
                default:
                    Console.WriteLine("Onimusha DualSense 1.2.0\nCommands: prepare-assets [--game <folder>] [--pak-tool <exe>] [--decoder <exe>], prepare-waves [--force], diagnose");
                    return command == "help" ? 0 : 1;
            }
        }
        catch (Exception e)
        {
            Console.Error.WriteLine("ERROR: " + e.Message);
            try { Files.Log(e.ToString()); } catch { }
            return 1;
        }
    }
}
