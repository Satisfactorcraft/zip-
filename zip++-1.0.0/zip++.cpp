#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstring>
#include <zlib.h>
#include <lzma.h>

namespace fs = std::filesystem;

// --- Globale Flags ---
bool verbose = false;
bool use_zlib = false;
bool use_lzma = false;

// --- ZIP-Strukturen ---
#pragma pack(push, 1)
struct LocalFileHeader {
    uint32_t signature = 0x04034b50;
    uint16_t version = 20;
    uint16_t flags = 0;
    uint16_t compression = 0;
    uint16_t modTime = 0;
    uint16_t modDate = 0;
    uint32_t crc32 = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint16_t filenameLength;
    uint16_t extraFieldLength = 0;
};

struct CentralDirectoryHeader {
    uint32_t signature = 0x02014b50;
    uint16_t versionMadeBy = 20;
    uint16_t versionNeeded = 20;
    uint16_t flags = 0;
    uint16_t compression = 0;
    uint16_t modTime = 0;
    uint16_t modDate = 0;
    uint32_t crc32 = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint16_t filenameLength;
    uint16_t extraFieldLength = 0;
    uint16_t fileCommentLength = 0;
    uint16_t diskNumber = 0;
    uint16_t internalAttr = 0;
    uint32_t externalAttr = 0;
    uint32_t localHeaderOffset;
};

struct EndOfCentralDirectory {
    uint32_t signature = 0x06054b50;
    uint16_t diskNumber = 0;
    uint16_t startDisk = 0;
    uint16_t entriesThisDisk;
    uint16_t entriesTotal;
    uint32_t cdSize;
    uint32_t cdOffset;
    uint16_t commentLength = 0;
};
#pragma pack(pop)

// --- CRC32 Platzhalter ---
uint32_t simple_crc32(const std::vector<char>& data) {
    uint32_t crc = 0;
    for(char c : data) crc += (unsigned char)c;
    return crc;
}

// --- zlib Kompression ---
std::vector<char> compress_zlib(const std::vector<char>& input){
    uLongf out_size = compressBound(input.size());
    std::vector<char> out(out_size);
    if(compress2((Bytef*)out.data(), &out_size, (const Bytef*)input.data(), input.size(), Z_BEST_COMPRESSION) != Z_OK)
        throw std::runtime_error("Zlib compression failed");
    out.resize(out_size);
    return out;
}

// --- LZMA Kompression ---
std::vector<char> compress_lzma(const std::vector<char>& input) {
    lzma_stream strm = LZMA_STREAM_INIT;
    if (lzma_easy_encoder(&strm, 6, LZMA_CHECK_CRC64) != LZMA_OK)
        throw std::runtime_error("LZMA init failed");

    strm.next_in = reinterpret_cast<const uint8_t*>(input.data());
    strm.avail_in = input.size();

    std::vector<char> out;
    size_t chunk = 65536;
    std::vector<char> buffer(chunk);

    lzma_ret ret;
    do {
        strm.next_out = reinterpret_cast<uint8_t*>(buffer.data());
        strm.avail_out = buffer.size();
        ret = lzma_code(&strm, LZMA_FINISH);
        if (ret != LZMA_OK && ret != LZMA_STREAM_END && ret != LZMA_BUF_ERROR)
            throw std::runtime_error("LZMA compression failed");
        size_t written = buffer.size() - strm.avail_out;
        out.insert(out.end(), buffer.data(), buffer.data() + written);
    } while (ret != LZMA_STREAM_END);

    lzma_end(&strm);
    return out;
}


// --- ZIP erstellen ---
void create_zip_store(const std::string& archive, const std::vector<std::string>& files){
    if(use_zlib && use_lzma){ std::cerr<<"Cannot use both -z and -l\n"; return; }

    std::ofstream out(archive,std::ios::binary);
    if(!out){ std::cerr<<"Cannot create archive\n"; return; }
    out.write("ZIP1",4);

    std::vector<CentralDirectoryHeader> central;
    std::vector<std::string> filenames;

    for(const auto& file : files){
        if(verbose) std::cout<<file<<"\n";
        std::ifstream in(file,std::ios::binary);
        if(!in){ std::cerr<<"File not found: "<<file<<"\n"; continue; }
        std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        LocalFileHeader lfh;
        lfh.filenameLength = file.size();
        std::vector<char> outdata;

        if(use_zlib){ outdata=compress_zlib(data); lfh.compression=8; }
        else if(use_lzma){ outdata=compress_lzma(data); lfh.compression=14; }
        else { outdata=data; lfh.compression=0; }

        lfh.uncompressedSize = data.size();
        lfh.compressedSize = outdata.size();
        lfh.crc32 = simple_crc32(data);

        uint32_t offset = (uint32_t)out.tellp();
        out.write((char*)&lfh,sizeof(lfh));
        out.write(file.c_str(),file.size());
        out.write(outdata.data(),outdata.size());

        CentralDirectoryHeader cdh;
        cdh.crc32=lfh.crc32;
        cdh.compressedSize=lfh.compressedSize;
        cdh.uncompressedSize=lfh.uncompressedSize;
        cdh.filenameLength=lfh.filenameLength;
        cdh.localHeaderOffset=offset;
        cdh.compression=lfh.compression;
        central.push_back(cdh);
        filenames.push_back(file);
    }

    uint32_t cdOffset = (uint32_t)out.tellp();
    for(size_t i=0;i<central.size();i++){
        out.write((char*)&central[i],sizeof(central[i]));
        out.write(filenames[i].c_str(),filenames[i].size());
    }

    uint32_t cdSize = (uint32_t)out.tellp() - cdOffset;
    EndOfCentralDirectory eocd;
    eocd.entriesThisDisk=central.size();
    eocd.entriesTotal=central.size();
    eocd.cdSize=cdSize;
    eocd.cdOffset=cdOffset;
    out.write((char*)&eocd,sizeof(eocd));
}

// --- ZIP auflisten ---
void list_zip(const std::string& archive){
    std::ifstream in(archive,std::ios::binary);
    if(!in){ std::cerr<<"Cannot open archive\n"; return; }
    char magic[4]; in.read(magic,4);
    if(std::string(magic,4)!="ZIP1"){ std::cerr<<"Unknown archive\n"; return; }

    in.seekg(-22,std::ios::end);
    EndOfCentralDirectory eocd; in.read((char*)&eocd,sizeof(eocd));
    in.seekg(eocd.cdOffset,std::ios::beg);

    for(int i=0;i<eocd.entriesTotal;i++){
        CentralDirectoryHeader cdh; in.read((char*)&cdh,sizeof(cdh));
        std::string fname(cdh.filenameLength,'\0'); in.read(&fname[0],fname.size());
        std::cout<<fname<<"\n";
    }
}

// --- ZIP entpacken ---
void extract_zip(const std::string& archive,const std::string& outdir){
    std::ifstream in(archive,std::ios::binary);
    if(!in){ std::cerr<<"Cannot open archive\n"; return; }
    char magic[4]; in.read(magic,4);
    if(std::string(magic,4)!="ZIP1"){ std::cerr<<"Unknown archive\n"; return; }

    in.seekg(-22,std::ios::end);
    EndOfCentralDirectory eocd; in.read((char*)&eocd,sizeof(eocd));
    in.seekg(eocd.cdOffset,std::ios::beg);

    for(int i=0;i<eocd.entriesTotal;i++){
        CentralDirectoryHeader cdh; in.read((char*)&cdh,sizeof(cdh));
        std::string fname(cdh.filenameLength,'\0'); in.read(&fname[0],fname.size());

        auto pos=in.tellg();
        std::ifstream in2(archive,std::ios::binary);
        in2.seekg(cdh.localHeaderOffset,std::ios::beg);
        LocalFileHeader lfh; in2.read((char*)&lfh,sizeof(lfh));
        std::string fname2(lfh.filenameLength,'\0'); in2.read(&fname2[0],fname2.size());
        std::vector<char> data(lfh.compressedSize); in2.read(data.data(),data.size());

        fs::create_directories(outdir);
        std::ofstream out(outdir+"/"+fname2,std::ios::binary);

        if(lfh.compression==8){
            std::vector<char> dec(lfh.uncompressedSize);
            uLongf destLen=lfh.uncompressedSize;
            if(uncompress((Bytef*)dec.data(), &destLen,(const Bytef*)data.data(),data.size())!=Z_OK)
                std::cerr<<"Zlib decompression failed\n";
            out.write(dec.data(),dec.size());
        }else if(lfh.compression==14){
            lzma_stream strm=LZMA_STREAM_INIT;
            if(lzma_stream_decoder(&strm, UINT64_MAX,LZMA_CONCATENATED)!=LZMA_OK){ std::cerr<<"LZMA init failed\n"; continue; }
            strm.next_in=(const uint8_t*)data.data();
            strm.avail_in=data.size();
            std::vector<char> dec(lfh.uncompressedSize);
            strm.next_out=(uint8_t*)dec.data();
            strm.avail_out=dec.size();
            if(lzma_code(&strm,LZMA_FINISH)!=LZMA_STREAM_END) std::cerr<<"LZMA decode failed\n";
            lzma_end(&strm);
            out.write(dec.data(),dec.size());
        }else out.write(data.data(),data.size());

        if(verbose) std::cout<<fname2<<"\n";
        in.seekg(pos);
    }
}

// --- Argumentparser ---
int main(int argc,char* argv[]){
    bool create=false,list=false,extract=false;
    std::string archive,outdir=".";
    std::vector<std::string> files;

    for(int i=1;i<argc;i++){
        std::string arg=argv[i];
        if(arg.size()>1 && arg[0]=='-'){
            for(size_t j=1;j<arg.size();j++){
                char opt=arg[j];
                switch(opt){
                    case 'c': create=true; break;
                    case 't': list=true; break;
                    case 'x': extract=true; break;
                    case 'v': verbose=true; break;
                    case 'z': use_zlib=true; break;
                    case 'l': use_lzma=true; break;
                    case 'f':
                        if(j+1<arg.size()){ archive=arg.substr(j+1); j=arg.size(); }
                        else if(i+1<argc) archive=argv[++i];
                        else { std::cerr<<"-f needs filename\n"; return 1; }
                        break;
                    case 'C':
                        if(i+1<argc) outdir=argv[++i];
                        else { std::cerr<<"-C needs directory\n"; return 1; }
                        break;
                    case 'V':
                        std::cout << "zip++ V1.0.0 Build 1\nMIT License";
                        return 0;
                    default: std::cerr<<"Unknown option: -"<<opt<<"\n"; return 1;
                }
            }
        }else files.push_back(arg);
    }

    if(create){
        if(archive.empty() || files.empty()){ std::cerr<<"Usage: -cv[z|l]f archive file...\n"; return 1; }
        create_zip_store(archive,files);
    }else if(list){
        if(archive.empty()){ std::cerr<<"Usage: -tvf archive\n"; return 1; }
        list_zip(archive);
    }else if(extract){
        if(archive.empty()){ std::cerr<<"Usage: -xvf archive [-C dir]\n"; return 1; }
        extract_zip(archive,outdir);
    }else{
        std::cerr<<"Usage: [-c|-t|-x] [-v] [-z|-l] -f archive [file...]\n";
        return 1;
    }
}
