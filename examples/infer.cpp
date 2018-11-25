// ======================================================================== //
// Copyright 2009-2018 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include <iostream>
#include <cassert>
#include <cmath>

#ifdef VTUNE
#include <ittnotify.h>
#endif

#include "OpenImageDenoise/oidn.hpp"
#include "common/timer.h"
#include "image_io.h"

using namespace std;
using namespace oidn;

int main(int argc, char **argv)
{
  oidn::DeviceRef device = oidn::newDevice();

  std::string inputFilename = "test0.tza";
  if (argc > 1)
    inputFilename = argv[1];
  std::string refFilename = inputFilename.substr(0, inputFilename.find_last_of('.')) + "_ref.tza";
  if (argc > 2)
      refFilename = argv[2];

  Tensor input = loadImageTZA(inputFilename);
  const int H = input.dims[0];
  const int W = input.dims[1];
  cout << inputFilename << ": " << W << "x" << H << endl;

  Tensor output({H, W, 3}, "hwc");

  Timer timer;

  oidn::FilterRef filter = device.newFilter("Autoencoder");

  const size_t F = sizeof(float);
  filter.setImage("color",  input.data,  oidn::Format::Float3, W, H, 0*F, 9*F);
  filter.setImage("albedo", input.data,  oidn::Format::Float3, W, H, 3*F, 9*F);
  filter.setImage("normal", input.data,  oidn::Format::Float3, W, H, 6*F, 9*F);
  filter.setImage("output", output.data, oidn::Format::Float3, W, H, 0*F, 3*F);
  filter.set1i("srgb", 1);
  //filter.set1i("hdr", 1);

  filter.commit();

  const char* errorMessage;
  if (device.getError(&errorMessage) != oidn::Error::None)
  {
    cout << "error: " << errorMessage << endl;
    exit(1);
  }

  double initd = timer.query();
  cout << "init=" << (1000. * initd) << " msec" << endl;

  // correctness check and warmup
  Tensor ref = loadImageTZA(refFilename);
  if (ref.dims != output.dims)
  {
    cout << "error: reference output size mismatch" << endl;
    exit(1);
  }
  filter.execute();
  int nerr = 0;
  float maxre = 0;
  for (size_t i = 0; i < output.size(); ++i)
  {
    float expect = ref.data[i];
    float actual = output.data[i];
    float re;
    if (abs(expect) < 1e-5 && abs(actual) < 1e-5)
      re = 0;
    else if (expect != 0)
      re = abs((expect-actual)/expect);
    else
      re = abs(expect-actual);
    if (maxre < re) maxre = re;
    if (re > 1e-5)
    {
      //cout << "i=" << i << " expect=" << expect << " actual=" << actual << endl;
      ++nerr;
    }
  }
  cout << "checked " << output.size() << " floats, nerr=" << nerr << ", maxre=" << maxre << endl;

  // save images
  saveImagePPM(output, "infer_out.ppm");
  saveImagePPM(ref,    "infer_ref.ppm");
  saveImagePPM(input,  "infer_in.ppm");

  // benchmark loop
  #ifdef VTUNE
  __itt_resume();
  #endif
  double mind = INFINITY;
  Timer totalTimer;
  cout << "===== start =====" << endl;
  int ntimes = 100;
  //int ntimes = 5;
  for (int i = 0; i < ntimes; ++i)
  {
    timer.reset();
    filter.execute();
    mind = min(mind, timer.query());
  }
  double totald = totalTimer.query();
  cout << "===== stop =====" << endl;
  cout << "ntimes=" << ntimes << " secs=" << totald
       << " msec/image=" << (1000.*totald/ntimes)
       << " (min=" << (1000.*mind) << ")" << endl;
  #ifdef VTUNE
  __itt_pause();
  #endif

  return 0;
}
