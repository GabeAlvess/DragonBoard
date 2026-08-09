using Mutagen.Bethesda;
using Mutagen.Bethesda.Plugins;
using Mutagen.Bethesda.Skyrim;

const uint physicalBoardLocalFormId = 0x000800;
const uint vrikProxyLocalFormId = 0x000801;
const uint vrikProxyFirstPersonLocalFormId = 0x000802;
const uint ironDaggerFormId = 0x01397E;
const uint belethorMerchantChestFormId = 0x09CAF8;
const int belethorStockCount = 3;
const string pluginName = "DragonBoardVR.esp";

var outputPath = args.Length > 0
    ? Path.GetFullPath(args[0])
    : Path.GetFullPath(Path.Combine(
        AppContext.BaseDirectory,
        "..",
        "..",
        "..",
        "..",
        "Assets",
        pluginName));

Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);

var skyrimMasterPath = ResolveSkyrimMasterPath(args);
var skyrimModKey = ModKey.FromFileName("Skyrim.esm");
var skyrim = SkyrimMod.CreateFromBinary(
    new ModPath(skyrimModKey, skyrimMasterPath),
    SkyrimRelease.SkyrimSE);

var modKey = ModKey.FromFileName(pluginName);
var mod = new SkyrimMod(modKey, SkyrimRelease.SkyrimSE);
mod.IsSmallMaster = true;
mod.ModHeader.Author = "GabeAlvz";
mod.ModHeader.Description =
    "Physical DragonBoard carrier used by the DragonBoardVR SKSE plugin.";

var physicalBoard = new MiscItem(
    new FormKey(modKey, physicalBoardLocalFormId),
    SkyrimRelease.SkyrimSE)
{
    EditorID = "DragonBoardVRPhysicalBoard",
    Name = "DragonBoard",
    Model = new Model
    {
        File = @"DragonBoardVR\dragonboard_physical.nif"
    },
    Value = 100,
    Weight = 2.0f
};

mod.MiscItems.Add(physicalBoard);

var ironDagger = skyrim.Weapons.FirstOrDefault(
    weapon => weapon.FormKey == new FormKey(skyrimModKey, ironDaggerFormId));
if (ironDagger is null)
{
    throw new InvalidOperationException(
        $"Could not find the Iron Dagger template 0x{ironDaggerFormId:X6} in {skyrimMasterPath}.");
}

var vrikProxyFirstPerson = new Static(
    new FormKey(modKey, vrikProxyFirstPersonLocalFormId),
    SkyrimRelease.SkyrimSE)
{
    EditorID = "DragonBoardVRVrikHolsterProxyFirstPerson",
    Model = new Model
    {
        File = @"DragonBoardVR\dragonboard_vrik_proxy.nif"
    }
};
mod.Statics.Add(vrikProxyFirstPerson);

var vrikProxy = mod.Weapons.DuplicateInAsNewRecord(
    ironDagger,
    new FormKey(modKey, vrikProxyLocalFormId));
vrikProxy.EditorID = "DragonBoardVRVrikHolsterProxy";
vrikProxy.Name = "DragonBoard (VRIK Holster)";
vrikProxy.Model = new Model
{
    File = @"DragonBoardVR\dragonboard_vrik_proxy.nif"
};
vrikProxy.FirstPersonModel.SetTo(vrikProxyFirstPerson.FormKey);
var proxyBasicStats = vrikProxy.BasicStats ??
    throw new InvalidOperationException("The Iron Dagger template has no basic weapon stats.");
var proxyData = vrikProxy.Data ??
    throw new InvalidOperationException("The Iron Dagger template has no weapon data.");
proxyBasicStats.Value = 0;
proxyBasicStats.Weight = 0.0f;
proxyBasicStats.Damage = 0;
proxyData.AnimationType = WeaponAnimationType.OneHandDagger;

var belethorMerchantChest = skyrim.Containers.FirstOrDefault(
    container => container.FormKey == new FormKey(skyrimModKey, belethorMerchantChestFormId));
if (belethorMerchantChest is null)
{
    throw new InvalidOperationException(
        $"Could not find Belethor's merchant chest 0x{belethorMerchantChestFormId:X6} in {skyrimMasterPath}.");
}

var belethorMerchantChestOverride = belethorMerchantChest.DeepCopy();
belethorMerchantChestOverride.Items ??= [];
belethorMerchantChestOverride.Items.Add(new ContainerEntry
{
    Item = new ContainerItem
    {
        Item = new FormLink<IItemGetter>(physicalBoard.FormKey),
        Count = belethorStockCount
    }
});
mod.Containers.Add(belethorMerchantChestOverride);

mod.WriteToBinary(outputPath);

Console.WriteLine($"Generated {outputPath}");
Console.WriteLine($"Physical board local FormID: 0x{physicalBoardLocalFormId:X6}");
Console.WriteLine($"VRIK proxy local FormID: 0x{vrikProxyLocalFormId:X6}");
Console.WriteLine($"VRIK visible setup first-person local FormID: 0x{vrikProxyFirstPersonLocalFormId:X6}");
Console.WriteLine($"VRIK proxy equipment type: {vrikProxy.EquipmentType.FormKey}");
Console.WriteLine($"Base value: {physicalBoard.Value} gold");
Console.WriteLine($"Belethor stock: {belethorStockCount} per merchant chest reset");

static string ResolveSkyrimMasterPath(string[] arguments)
{
    if (arguments.Length > 1)
    {
        return RequireExistingFile(arguments[1]);
    }

    var configuredPath = Environment.GetEnvironmentVariable("SKYRIMVR_MASTER_PATH");
    if (!string.IsNullOrWhiteSpace(configuredPath))
    {
        return RequireExistingFile(configuredPath);
    }

    var candidates = new[]
    {
        @"I:\Games\Skyrim VR FUS\Game Root\Data\Skyrim.esm",
        @"I:\Games\Skyrim VR FUS\mods\Cleaned Master Files\Skyrim.esm"
    };

    var discoveredPath = candidates.FirstOrDefault(File.Exists);
    if (discoveredPath is not null)
    {
        return discoveredPath;
    }

    throw new FileNotFoundException(
        "Skyrim.esm was not found. Pass its path as the second argument or set SKYRIMVR_MASTER_PATH.");
}

static string RequireExistingFile(string path)
{
    var fullPath = Path.GetFullPath(path);
    if (!File.Exists(fullPath))
    {
        throw new FileNotFoundException("Skyrim.esm was not found.", fullPath);
    }

    return fullPath;
}


