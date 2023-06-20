#include <iostream>
#include <fstream>
#include <string>
#include <stdio.h>
#include <sys/stat.h>
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Usage: ./resize_img <image path>" << std::endl;
        return -1;
    }
    auto file_path = std::string(argv[1]);
    std::ifstream infile;
    struct stat file_stat;
    size_t size;
    if(stat(file_path.c_str(), &file_stat) == 0) {
        size = file_stat.st_size;
    } else {
        std::cerr << "Failed to stat file " << file_path << std::endl;
        return 1;
    }

    std::cout << file_path << " has " << size << " bytes." << std::endl;
    std::vector<char> *buffer = new std::vector<char>(size);
    infile.open(file_path, std::ios::in|std::ios::binary);
    infile.read(buffer->data(), size);

    std::cout << "Read " << infile.gcount() << " bytes data from " << file_path << std::endl;
    infile.close();
    cv::Mat img = cv::imdecode(*buffer, cv::IMREAD_COLOR);
    cv::Mat resized_img;
    cv::resize(img, resized_img, cv::Size(800, 600), 0, 0);
    std::vector<uchar> *output_buffer = new std::vector<uchar>(800 * 600 * 4);
    if(cv::imencode(".png", resized_img, *output_buffer)){
        std::ofstream outfile;
        outfile.open("resized.png", std::ios::out|std::ios::binary);
        outfile.write(reinterpret_cast<char*>(output_buffer->data()), output_buffer->size());
        std::cout << "Write " << output_buffer->size() << " bytes data to resized.png" << std::endl;
        outfile.flush();
        outfile.close();
    }
    delete buffer;
    delete output_buffer;
    return 0;
}
