/*
*  SICAK - SIde-Channel Analysis toolKit
*  Copyright (C) 2018-2019 Petr Socha, FIT, CTU in Prague
*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/**
* \file random128apdu.cpp
*
* \brief SICAK CPA Measurement scenario plugin: sends N times a command APDU: CLA=0x80, INS=0x60, P1=P2=0x00, Lc=0x10, Le=0x10 with 16 bytes of random data and receives 16 bytes of ciphertexts back. Makes use of multiple captures per oscilloscope run (e.g. Picoscope's rapid block mode).
*
*
* \author Petr Socha
* \version 1.0
*/

#include <QString>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <random>
#include "random128apdu.h"
#include "filehandling.hpp"
#include "global_calls.hpp"

Random128APDU::Random128APDU(){
    
}

Random128APDU::~Random128APDU(){
    
}

QString Random128APDU::getPluginName() {
    return "AES-128 random (APDU oriented)";
}

QString Random128APDU::getPluginInfo() {
    return "Sends N times a command APDU: CLA=0x80, INS=0x60, P1=P2=0x00, Lc=0x10, Le=0x10 with 16 bytes of random data, receives Response APDUs with 16 bytes of ciphertext back, and captures the power consumption.";
}

void Random128APDU::init(const char * param){
    Q_UNUSED(param)
}

void Random128APDU::deInit(){
    
}

void Random128APDU::run(const char * measurementId, size_t measurements, Oscilloscope * oscilloscope, CharDevice * charDevice){
    
    if(oscilloscope == nullptr || charDevice == nullptr){
        throw RuntimeException("Oscilloscope and character device are needed to run this measurement.");
    }
    
    size_t samplesPerTrace;
    size_t capturesPerRun;
    
    oscilloscope->getCurrentSetup(samplesPerTrace, capturesPerRun); //< Retrieve oscilloscope setup    
    
    // Check the input params
    if(measurements < capturesPerRun){
        throw InvalidInputException("Oscilloscope and measurement parameter mismatch: number of measurements must be greater or equal to number of oscilloscope captures");
    } else if(measurements % capturesPerRun){                
        throw InvalidInputException("Oscilloscope and measurement parameter mismatch: number of measurements must be divisible by the number of oscilloscope captures without remainder");
    }        
    
    CoutProgress::get().start(measurements);
        
    // Alloc space
    Matrix<uint8_t> plaintext(16, measurements);
    Matrix<uint8_t> ciphertext(16, measurements);
    PowerTraces<int16_t> measuredTraces(samplesPerTrace, measurements);  
    Vector<uint8_t> commandAPDU(16+6); // 6 bytes APDU fields + 16 bytes of AES plaintext
    Vector<uint8_t> responseAPDU(16+2); // 2 bytes APDU fields + 16 bytes of AES plaintext
    
    QString tracesFilename = "random-traces-";
    tracesFilename.append(measurementId);
    tracesFilename.append(".bin");
    QString plaintextFilename = "plaintext-";
    plaintextFilename.append(measurementId);
    plaintextFilename.append(".bin");
    QString ciphertextFilename = "ciphertext-";
    ciphertextFilename.append(measurementId);
    ciphertextFilename.append(".bin");
    
    QByteArray ba;
    
    // Open files
    ba = tracesFilename.toLocal8Bit();
    std::fstream tracesFile = openOutFile(ba.data());
    ba = plaintextFilename.toLocal8Bit();
    std::fstream plaintextFile = openOutFile(ba.data());
    ba = ciphertextFilename.toLocal8Bit();
    std::fstream ciphertextFile = openOutFile(ba.data());
        
    // Prepare the command APDU
    commandAPDU(0) = 0x80; // CLA
    commandAPDU(1) = 0x60; // INS
    commandAPDU(2) = 0x00; // P1
    commandAPDU(3) = 0x00; // P2
    commandAPDU(4) = 0x10; // Lc
    // commandAPDU(5:20) = <random data>
    commandAPDU(21) = 0x10; // Le
    
    // Random number generation
    std::random_device trng;
    std::mt19937 prng(trng());
    std::uniform_int_distribution<std::mt19937::result_type> byteUnif(0, 255);
    
    // Measure
    size_t runs = measurements / capturesPerRun; //< Number of independent oscilloscope runs
    
    // We run the oscilloscope runs times
    for(size_t run = 0; run < runs; run++){
        
        oscilloscope->run(); //< Start capturing capturesPerRun captures
        
        // Send capturesPerRun blocks to cipher
        for(size_t capture = 0; capture < capturesPerRun; capture++){                     
            
            size_t measurement = run * capturesPerRun + capture; //< Number of current measurement
            
            // Generate random plaintext
            for(int byte = 0; byte < 16; byte++){
                
                plaintext(byte, measurement) = (uint8_t) byteUnif(prng);
                          
            }
            
            // Send plaintext
            
            // fill APDU with plaintext
            for(int byte = 0; byte < 16; byte++){
                commandAPDU(5+byte) = plaintext(byte, measurement);
            }
            // send APDU
            charDevice->send(commandAPDU);
            
            // Receive ciphertext
            
            // receive APDU
            if(charDevice->receive(responseAPDU) != 18) throw RuntimeException("Failed to receive 18 bytes APDU response (16 bytes ciphertext + SW1 + SW2).");
            // copy ciphertext
            for(int byte = 0; byte < 16; byte++){
                ciphertext(byte, measurement) = responseAPDU(byte);
            }
            
            CoutProgress::get().update(measurement);
            
        }
        
        size_t measuredSamples;
        size_t measuredCaptures;
        
        // Download the sampled data from oscilloscope
        oscilloscope->getValues(1, &( measuredTraces(0, run * capturesPerRun) ), capturesPerRun * samplesPerTrace, measuredSamples, measuredCaptures);
        
        if(measuredSamples != samplesPerTrace || measuredCaptures != capturesPerRun){
            throw RuntimeException("Measurement went wrong: samples*captures mismatch");
        }
        
    }
    
    // Write data to files
    writeArrayToFile(tracesFile, measuredTraces);
    writeArrayToFile(plaintextFile, plaintext);
    writeArrayToFile(ciphertextFile, ciphertext);
    
    // Close files
    closeFile(tracesFile);
    closeFile(plaintextFile);
    closeFile(ciphertextFile);
    
    CoutProgress::get().finish();
    
    // Flush config to json files
    QJsonObject tracesConf;
    tracesConf["random-traces"] = tracesFilename;
    tracesConf["random-traces-count"] = QString::number(measurements);
    tracesConf["samples-per-trace"] = QString::number(samplesPerTrace);
    tracesConf["blocks"] = plaintextFilename;
    tracesConf["blocks"] = ciphertextFilename;
    tracesConf["blocks-count"] = QString::number(measurements);    
    tracesConf["blocks-length"] = QString::number(16);
    QJsonDocument tracesDoc(tracesConf);
    QString tracesDocFilename = measurementId;
    tracesDocFilename.append(".json");
    QFile tracesDocFile(tracesDocFilename);
    if(tracesDocFile.open(QIODevice::WriteOnly)){
        tracesDocFile.write(tracesDoc.toJson());
    }
    
    QTextStream cout(stdout);
    cout << QString("Measured %1 power traces, %5 samples per trace, and saved them to '%2'.\nUsed plaintext blocks were saved to '%3', retrieved ciphertext blocks were saved to '%4'.\n").arg(measurements).arg(tracesFilename).arg(plaintextFilename).arg(ciphertextFilename).arg(samplesPerTrace);
    
}