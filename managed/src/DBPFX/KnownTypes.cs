using System.Collections.Generic;

namespace DBPFX
{
    /// <summary>What a resource type ID means in one game (or in several, when they agree).</summary>
    public sealed class ResourceTypeInfo
    {
        public ResourceTypeInfo(uint id, string tag, string name, string category, Games games)
        {
            Id = id;
            Tag = tag ?? string.Empty;
            Name = name ?? string.Empty;
            Category = category ?? string.Empty;
            Games = games;
        }

        public uint Id { get; }
        /// <summary>Four-letter tag where the community has one (mostly The Sims 3), otherwise empty.</summary>
        public string Tag { get; }
        /// <summary>Readable name, e.g. "String table".</summary>
        public string Name { get; }
        /// <summary>Area of the game, e.g. "Build", "Create-A-Sim", "Tuning" (The Sims 4 types only).</summary>
        public string Category { get; }
        /// <summary>Games in which the ID has this meaning.</summary>
        public Games Games { get; }

        /// <summary>The tag when there is one, otherwise the name.</summary>
        public string ShortName => Tag.Length > 0 ? Tag : Name;

        public override string ToString() => Id.ToString("X8") + " " + Name;
    }

    /// <summary>
    /// Resource types of The Sims 3, The Sims Medieval and The Sims 4, from data/resource-types.tsv.
    /// The same ID can mean different things in different games (0x0333406C is XML in The Sims 3 and a
    /// font configuration in The Sims 4), so look types up for a specific game where possible.
    /// </summary>
    public static partial class KnownTypes
    {
        public const uint StringTable = 0x220557DA;
        public const uint NameMap = 0x0166038C;
        public const uint DdsImage = 0x00B2D882;
        public const uint PngImage = 0x2F7D0004;
        public const uint CasPart = 0x034AEECB;
        public const uint Geometry = 0x015A1849;
        public const uint Model = 0x01661233;
        public const uint ModelLod = 0x01D10F34;
        public const uint VisualProxy = 0x736884F1;
        public const uint Rig = 0x8EAF13DE;
        public const uint AnimationClip = 0x6B20C4F3;
        public const uint StateMachine = 0x02D5DF13;
        public const uint ScriptAssembly = 0x073FAA07;
        public const uint XmlTuning = 0x0333406C;
        public const uint SimData = 0x545AC67A;
        public const uint CombinedTuning = 0x62E94D38;
        public const uint ObjectDefinition = 0xC0DB5AE7;
        public const uint Rle2Image = 0x3453CF95;
        public const uint RlesImage = 0xBA856C78;

        private static readonly object Gate = new object();
        private static readonly Dictionary<uint, List<ResourceTypeInfo>> Table = Build();

        private static Dictionary<uint, List<ResourceTypeInfo>> Build()
        {
            var rows = new List<ResourceTypeInfo>(512);
            Fill(rows);
            var table = new Dictionary<uint, List<ResourceTypeInfo>>(rows.Count);
            foreach (ResourceTypeInfo row in rows) Insert(table, row);
            return table;
        }

        static partial void Fill(List<ResourceTypeInfo> table);

        private static void Insert(Dictionary<uint, List<ResourceTypeInfo>> table, ResourceTypeInfo row)
        {
            if (!table.TryGetValue(row.Id, out var list))
            {
                list = new List<ResourceTypeInfo>(2);
                table.Add(row.Id, list);
            }
            // a registration for the same games replaces the earlier meaning for those games
            list.RemoveAll(existing => (existing.Games & ~row.Games) == Games.None);
            list.Add(row);
        }

        /// <summary>Add or override a type, e.g. one introduced by a newer expansion.</summary>
        public static void Register(uint id, string tag, string name, Games games, string category = "")
        {
            lock (Gate) Insert(Table, new ResourceTypeInfo(id, tag, name, category, games));
        }

        /// <summary>The meaning of <paramref name="id"/> in <paramref name="game"/>.</summary>
        public static bool TryGet(uint id, Game game, out ResourceTypeInfo info)
        {
            Games mask = (Games)(1 << (int)game);
            lock (Gate)
            {
                if (Table.TryGetValue(id, out var list))
                {
                    for (int i = list.Count - 1; i >= 0; i--)
                    {
                        if ((list[i].Games & mask) != Games.None)
                        {
                            info = list[i];
                            return true;
                        }
                    }
                }
            }
            info = null!;
            return false;
        }

        /// <summary>A meaning of <paramref name="id"/> in any game (The Sims 3 first, then The Sims 4).</summary>
        public static bool TryGet(uint id, out ResourceTypeInfo info) =>
            TryGet(id, Game.Sims3, out info) || TryGet(id, Game.Sims4, out info);

        /// <summary>Every known meaning of an ID, one per game group.</summary>
        public static IReadOnlyList<ResourceTypeInfo> Lookup(uint id)
        {
            lock (Gate) return Table.TryGetValue(id, out var list) ? list.ToArray() : new ResourceTypeInfo[0];
        }

        public static bool IsKnown(uint id, Game game) => TryGet(id, game, out _);

        /// <summary>Readable name of a type in a game, or its hexadecimal ID when unknown.</summary>
        public static string NameOf(uint id, Game game) => TryGet(id, game, out var info) ? info.Name : id.ToString("X8");

        /// <summary>Short name (tag, or name when there is no tag), or the hexadecimal ID when unknown.</summary>
        public static string TagOf(uint id, Game? game = null)
        {
            ResourceTypeInfo info;
            bool found = game.HasValue ? TryGet(id, game.Value, out info) : TryGet(id, out info);
            return found ? info.ShortName : id.ToString("X8");
        }

        public static IReadOnlyCollection<ResourceTypeInfo> All
        {
            get
            {
                lock (Gate)
                {
                    var all = new List<ResourceTypeInfo>();
                    foreach (var list in Table.Values) all.AddRange(list);
                    return all;
                }
            }
        }
    }
}
