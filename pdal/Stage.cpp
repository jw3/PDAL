/******************************************************************************
* Copyright (c) 2011, Michael P. Gerlek (mpg@flaxen.com)
*
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following
* conditions are met:
*
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in
*       the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
* COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
* OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
* AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
* OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
* OF SUCH DAMAGE.
****************************************************************************/

#include <pdal/GDALUtils.hpp>
#include <pdal/GEOSUtils.hpp>
#include <pdal/PipelineManager.hpp>
#include <pdal/Stage.hpp>
#include <pdal/SpatialReference.hpp>
#include <pdal/PDALUtils.hpp>
#include <pdal/util/Algorithm.hpp>
#include <pdal/util/ProgramArgs.hpp>

#include "private/StageRunner.hpp"

#include <iterator>
#include <memory>

namespace pdal
{

Stage::Stage() : m_progressFd(-1), m_verbose(0), m_pointCount(0),
    m_faceCount(0)
{}


void Stage::addConditionalOptions(const Options& opts)
{
    for (const auto& o : opts.getOptions())
        m_options.addConditional(o);
}


void Stage::serialize(MetadataNode root, PipelineWriter::TagMap& tags) const
{
    for (Stage *s : m_inputs)
        s->serialize(root, tags);

    auto tagname = [tags](const Stage *s)
    {
        const auto ti = tags.find(s);
        return ti->second;
    };

    MetadataNode anon("pipeline");
    anon.add("type", getName());
    anon.add("tag", tagname(this));
    m_options.toMetadata(anon);
    for (Stage *s : m_inputs)
        anon.addList("inputs", tagname(s));
    root.addList(anon);
}

void Stage::addAllArgs(ProgramArgs& args)
{
    try
    {
        l_addArgs(args);
        addArgs(args);
    }
    catch (arg_error error)
    {
        throw pdal_error(getName() + ": " + error.m_error);
    }
}


void Stage::handleOptions()
{
    addAllArgs(*m_args);

    StringList files = m_options.getValues("option_file");
    for (std::string& file : files)
        m_options.addConditional(Options::fromFile(file));
    m_options.remove(Option("option_file", 0));

    // Special stuff for GRiD so that no error is thrown when a file
    // isn't found.
    files = m_options.getValues("grid_option_file");
    for (std::string& file : files)
        m_options.addConditional(Options::fromFile(file, false));
    m_options.remove(Option("grid_option_file", 0));

    StringList cmdline = m_options.toCommandLine();
    try
    {
        m_args->parse(cmdline);
    }
    catch (arg_error error)
    {
        throw pdal_error(getName() + ": " + error.m_error);
    }
    setupLog();
}


QuickInfo Stage::preview()
{
    m_args.reset(new ProgramArgs);
    handleOptions();
    startLogging();
    QuickInfo qi = inspect();
    stopLogging();
    return qi;
}


void Stage::prepare(PointTableRef table)
{
    m_args.reset(new ProgramArgs);
    for (size_t i = 0; i < m_inputs.size(); ++i)
    {
        Stage *prev = m_inputs[i];
        prev->prepare(table);
    }
    handleOptions();
    startLogging();
    l_initialize(table);
    initialize(table);
    addDimensions(table.layout());
    prepared(table);
    stopLogging();
}


PointViewSet Stage::execute(PointTableRef table)
{
    startLogging();
    table.finalize();

    PointViewSet views;

    // If the inputs are empty, we're a reader.
    if (m_inputs.empty())
    {
        m_log->get(LogLevel::Debug) << "Executing pipeline in standard mode." <<
            std::endl;
        views.insert(PointViewPtr(new PointView(table)));
    }
    else
    {
        for (size_t i = 0; i < m_inputs.size(); ++i)
        {
            Stage *prev = m_inputs[i];
            PointViewSet temp = prev->execute(table);
            views.insert(temp.begin(), temp.end());
        }
    }

    PointViewSet outViews;
    std::vector<StageRunnerPtr> runners;

    // Put the spatial references from the views onto the table.
    // The table's spatial references are only valid as long as the stage
    // is running.
    // ABELL - Should we clear the references once the stage run has
    //   completed?  Wondering if that would break something where a
    //   writer wants to check a table's SRS.
    SpatialReference srs;
    table.clearSpatialReferences();
    // Iterating backwards will ensure that the SRS for the first view is
    // first on the list for table.
    for (auto it = views.rbegin(); it != views.rend(); it++)
        table.addSpatialReference((*it)->spatialReference());

    // Count the number of views and the number of points and faces so they're
    // available to stages.
    m_pointCount = 0;
    m_faceCount = 0;
    for (auto const& it : views)
    {
        m_pointCount += it->size();
        auto m = it->mesh();
        if (m)
            m_faceCount += m->size();
    }
    // Do the ready operation and then start running all the views
    // through the stage.
    ready(table);
    for (auto const& it : views)
    {
        StageRunnerPtr runner(new StageRunner(this, it));
        runners.push_back(runner);
        runner->run();
    }

    // As the stages complete (synchronously at this time), propagate the
    // spatial reference and merge the output views.
    srs = getSpatialReference();
    for (auto const& it : runners)
    {
        StageRunnerPtr runner(it);
        PointViewSet temp = runner->wait();

        // If our stage has a spatial reference, the view takes it on once
        // the stage has been run.
        if (!srs.empty())
            for (PointViewPtr v : temp)
                v->setSpatialReference(srs);
        outViews.insert(temp.begin(), temp.end());
    }
    l_done(table);
    stopLogging();
    m_pointCount = 0;
    m_faceCount = 0;
    return outViews;
}


void Stage::l_done(PointTableRef table)
{
    done(table);
}

void Stage::l_addArgs(ProgramArgs& args)
{
    args.add("user_data", "User JSON", m_userDataJSON);
    args.add("log", "Debug output filename", m_logname);
    // We never really bind anything to this variable.  We extract the option
    // before parsing the command line.  This entry allows a line in the
    // help and options list.
    args.add("option_file", "File from which to read additional options",
        m_optionFile);
    readerAddArgs(args);
}


void Stage::setupLog()
{
    LogLevel l(LogLevel::Error);

    if (m_log)
    {
        l = m_log->getLevel();
        m_logLeader = m_log->leader();
    }

    if (!m_logname.empty())
        m_log.reset(new Log("", m_logname));
    else if (!m_log)
        m_log.reset(new Log("", "stdlog"));
    m_log->setLevel(l);

    // Add the stage name to the existing leader.
    if (m_logLeader.size())
        m_logLeader += " ";
    m_logLeader += getName();

    gdal::ErrorHandler::getGlobalErrorHandler().set(m_log, isDebug());
}


void Stage::l_initialize(PointTableRef table)
{
    m_metadata = table.metadata().add(getName());
    writerInitialize(table);
}


// This function allows m_spatialReference to remain private.
void Stage::addSpatialReferenceArg(ProgramArgs& args)
{
    args.add("spatialreference", "Spatial reference to apply to data",
        m_spatialReference);
}

const SpatialReference& Stage::getSpatialReference() const
{
    return m_spatialReference;
}


void Stage::setSpatialReference(const SpatialReference& spatialRef)
{
    setSpatialReference(m_metadata, spatialRef);
}


void Stage::setSpatialReference(MetadataNode& m,
    const SpatialReference& spatialRef)
{
    m_spatialReference = spatialRef;

    auto pred = [](MetadataNode m){ return m.name() == "spatialreference"; };

    MetadataNode spatialNode = m.findChild(pred);
    if (spatialNode.empty())
    {
        m.add(spatialRef.toMetadata());
        m.add("spatialreference", spatialRef.getWKT(), "SRS of this stage");
        m.add("comp_spatialreference", spatialRef.getWKT(),
            "SRS of this stage");
    }
}


bool Stage::parseName(std::string o, std::string::size_type& pos)
{
    auto isStageChar = [](char c)
        { return std::isalnum(c) || c == '_'; };

    std::string::size_type start = pos;
    if (!std::isalpha(o[pos]))
        return false;
    pos++;
    pos += Utils::extract(o, pos, isStageChar);
    return true;
}


bool Stage::parseTagName(std::string o, std::string::size_type& pos)
{
    auto isTagChar = [](char c)
        { return std::isalnum(c) || c == '_'; };

    std::string::size_type start = pos;
    if (!std::isalpha(o[pos]))
        return false;
    pos++;
    pos += Utils::extract(o, pos, isTagChar);
    return true;
}


void Stage::throwError(const std::string& s) const
{
    throw pdal_error(getName() + ": " + s);
}


void Stage::startLogging() const
{
    m_log->pushLeader(m_logLeader);
    gdal::ErrorHandler::getGlobalErrorHandler().set(m_log, isDebug());
}


void Stage::stopLogging() const
{
    m_log->popLeader();
}

} // namespace pdal

