// DBPFX.Inspect - small example tool
//   DBPFX.Inspect info    <file.package>
//   DBPFX.Inspect strings <file.package>
//   DBPFX.Inspect convert <in.package> <out.package> <Sims3|Medieval|Sims4>
using System;
using System.Linq;
using DBPFX;
using DBPFX.Formats;

if (args.Length < 2)
{
    Console.WriteLine("usage: DBPFX.Inspect info|strings|convert <file.package> [...]");
    return 2;
}

using var package = PackageFile.Open(args[1]);
switch (args[0])
{
    case "info":
        Console.WriteLine($"{package} - {package.Game} - {Codec.EngineVersion}");
        foreach (var group in package.GroupBy(e => e.Type).OrderByDescending(g => g.Count()))
            Console.WriteLine($"  {group.Key:X8}  {group.Count(),7}  {KnownTypes.NameOf(group.Key, package.Game)}");
        return 0;

    case "strings":
        foreach (ResourceEntry entry in package.FindAll(KnownTypes.StringTable))
        {
            using ResourceLease data = package.Read(entry);   // mapped when stored, pooled when compressed
            var table = new StringTableReader(data.Span);
            Console.WriteLine($"{entry.Key}  {table.Format}, {table.Count} strings");
            foreach (StringTableItem item in table)
                Console.WriteLine($"  {item.Key:X16}  {item.Text}");
        }
        return 0;

    case "convert" when args.Length >= 4:
        var target = Enum.Parse<Game>(args[3], ignoreCase: true);
        using (PackageWriter writer = package.Edit(target))
            writer.Save(args[2]);
        Console.WriteLine($"{args[2]} written for {target}");
        return 0;

    default:
        Console.WriteLine("unknown command");
        return 2;
}
