# ModernSearchBar-Plugin

A modern Rainmeter plugin that displays Chrome browsing history and Google trending searches with a sleek, interactive UI.

## Features

- **Chrome History Integration** - Displays your recent Chrome browsing history
- **Google Trends** - Shows current trending searches by country
- **Parent/Child Architecture** - Efficient data fetching with index-based access
- **Asynchronous Loading** - Background data updates with cache fallback
- **Modern UI** - Stylish interface with hover effects and gradient accents
- **Multi-Profile Support** - Access different Chrome profiles

## Installation

1. Build the plugin using Visual Studio or run `Build-CPP.ps1`
2. Copy the compiled DLL to your Rainmeter Plugins folder
3. Load the skin from `Resources\Skins\ModernSearchBar-Plugin\Main.ini`

## Usage

### Parent Measure

The parent measure fetches all data:

```ini
[MeasureParent]
Measure=Plugin
Plugin=ModernSearchBar.dll
Type=Chrome_History           ; or Top_Trends
Profile=Default               ; Chrome profile name
CountryCode=US                ; For Top_Trends (US, UK, etc.)
OnCompleteAction=[!UpdateMeter *][!Redraw]
```

### Child Measures

Child measures access individual items by index:

```ini
[MeasureChild1]
Measure=Plugin
Plugin=ModernSearchBar.dll
ParentName=MeasureParent
Index=1

[MeasureChild2]
Measure=Plugin
Plugin=ModernSearchBar.dll
ParentName=MeasureParent
Index=2
```

## Parameters

### Parent Measure Options

| Parameter | Values | Description |
|-----------|--------|-------------|
| `Type` | `Chrome_History`, `Top_Trends` | Data source type |
| `Profile` | String (default: `Default`) | Chrome profile name |
| `CountryCode` | String (default: `US`) | Country code for trends |
| `OnCompleteAction` | Rainmeter bang | Action to execute when data loads |

### Child Measure Options

| Parameter | Values | Description |
|-----------|--------|-------------|
| `ParentName` | String | Name of the parent measure |
| `Index` | Integer (default: `1`) | Item index (1-based) |

## Technical Details

- **Language**: C++17
- **Dependencies**: SQLite3, WinINet, Rainmeter API
- **Architecture**: Parent/child pattern with thread-safe async updates
- **Caching**: Maintains previous results during background updates
- **RSS Filtering**: Automatically filters out URLs and metadata from trends

## Example Skin

See `Resources\Skins\ModernSearchBar-Plugin\Main.ini` for a complete example with:
- Modern gradient header and footer
- Two-section layout (Chrome History + Trending Searches)
- Hover effects and interactive elements
- Responsive design with custom colors

## License

MIT License

## Credits

Developed by nstechbytes (nstechbytes@gmail.com)