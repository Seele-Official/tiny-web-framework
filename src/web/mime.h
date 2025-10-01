#include <unordered_map>
#include <string>


namespace web {

inline const static std::unordered_map<std::string, std::string> mime_types = {
    // Text and Web Files
    {".html", "text/html"},
    {".htm", "text/html"},
    {".xhtml", "application/xhtml+xml"},
    {".shtml", "text/html"},
    {".txt", "text/plain"},
    {".text", "text/plain"},
    {".log", "text/plain"},
    {".md", "text/markdown"},
    {".markdown", "text/markdown"},
    {".css", "text/css"},
    {".csv", "text/csv"},
    {".rtf", "text/rtf"},

    // Scripts and Code
    {".js", "application/javascript"},
    {".mjs", "application/javascript"},
    {".cjs", "application/javascript"},
    {".json", "application/json"},
    {".jsonld", "application/ld+json"},
    {".xml", "application/xml"},
    {".xsd", "application/xml"},
    {".dtd", "application/xml-dtd"},
    {".plist", "application/xml"},
    {".yaml", "application/yaml"},
    {".yml", "application/yaml"},

    // Images
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".jpe", "image/jpeg"},
    {".jfif", "image/jpeg"},
    {".pjpeg", "image/jpeg"},
    {".pjp", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".bmp", "image/bmp"},
    {".ico", "image/x-icon"},
    {".cur", "image/x-icon"},
    {".svg", "image/svg+xml"},
    {".svgz", "image/svg+xml"},
    {".webp", "image/webp"},
    {".tiff", "image/tiff"},
    {".tif", "image/tiff"},
    {".psd", "image/vnd.adobe.photoshop"},

    // Audio and Video
    {".mp3", "audio/mpeg"},
    {".ogg", "audio/ogg"},
    {".wav", "audio/wav"},
    {".weba", "audio/webm"},
    {".aac", "audio/aac"},
    {".flac", "audio/flac"},
    {".mid", "audio/midi"},
    {".midi", "audio/midi"},
    {".mp4", "video/mp4"},
    {".webm", "video/webm"},
    {".ogv", "video/ogg"},
    {".avi", "video/x-msvideo"},
    {".mov", "video/quicktime"},
    {".wmv", "video/x-ms-wmv"},
    {".flv", "video/x-flv"},
    {".mpeg", "video/mpeg"},
    {".mpg", "video/mpeg"},

    // Archives and Binary
    {".zip", "application/zip"},
    {".rar", "application/x-rar-compressed"},
    {".7z", "application/x-7z-compressed"},
    {".tar", "application/x-tar"},
    {".gz", "application/gzip"},
    {".bz2", "application/x-bzip2"},
    {".xz", "application/x-xz"},
    {".pdf", "application/pdf"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".xls", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".ppt", "application/vnd.ms-powerpoint"},
    {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".odt", "application/vnd.oasis.opendocument.text"},
    {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {".odp", "application/vnd.oasis.opendocument.presentation"},

    // WebAssembly and Binary Data
    {".wasm", "application/wasm"},
    {".bin", "application/octet-stream"},
    {".exe", "application/octet-stream"},
    {".dll", "application/octet-stream"},
    {".so", "application/octet-stream"},
    {".dmg", "application/octet-stream"},
    {".deb", "application/octet-stream"},
    {".rpm", "application/octet-stream"},

    // Fonts
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {".otf", "font/otf"},
    {".eot", "application/vnd.ms-fontobject"},

    // Miscellaneous
    {".ics", "text/calendar"},
    {".sh", "application/x-sh"},
    {".php", "application/x-httpd-php"},
    {".swf", "application/x-shockwave-flash"},
    {".apk", "application/vnd.android.package-archive"},
    {".torrent", "application/x-bittorrent"},
    {".epub", "application/epub+zip"}
};

}
